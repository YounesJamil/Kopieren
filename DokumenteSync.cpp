// DokumenteSync - holt alles aus dem Dokumente-Ordner auf C nach Y.
//
//  - Fehlt eine Datei auf Y, wird sie von C nach Y kopiert.
//  - Gibt es sie auf beiden Seiten, gewinnt das neuere Aenderungsdatum:
//    ist C neuer, wird Y ueberschrieben; ist Y neuer, bleibt Y wie es ist.
//  - Identische Dateien (Datum gleich, +/- 2 Sekunden) werden uebersprungen.
//  - C wird nie veraendert, und es wird NIE etwas geloescht.
//  - Cache-Ordner (Traktor-Stripes, Browser-Caches usw.) werden uebersprungen.
//
// Mit --umleiten wird danach geprueft, ob alles auf Y angekommen ist. Dann wird
// der Ordner auf C in "..._alt" umbenannt und an seiner Stelle eine Junction
// auf Y angelegt. Programme, die fest nach C schreiben, landen so auf Y.
//
// Aufruf:
//   DokumenteSync.exe [--probelauf] [--umleiten] [--mit-caches] [--c PFAD] [--y PFAD]
//
// Bauen (C++17):
//   MSVC : cl /std:c++17 /EHsc /O2 /utf-8 DokumenteSync.cpp
//   MinGW: g++ -std=c++17 -O2 -municode -static DokumenteSync.cpp -o DokumenteSync.exe -lshell32 -lole32 -luuid

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <shlobj.h>
#  include <cwctype>
#  ifdef _MSC_VER
#    pragma comment(lib, "shell32.lib")
#    pragma comment(lib, "ole32.lib")
#  endif
#endif

namespace fs = std::filesystem;

namespace {

// Toleranz beim Datumsvergleich (FAT32/exFAT speichern nur auf 2 Sekunden genau).
constexpr auto kToleranz = std::chrono::seconds(2);

struct Datei {
    fs::path relativ;           // Pfad relativ zum Wurzelordner (Originalschreibweise)
    fs::file_time_type zeit;
    std::uintmax_t groesse = 0;
};

struct Bestand {
    std::map<std::string, Datei> dateien;   // Schluessel: normalisierter relativer Pfad
    std::map<std::string, fs::path> ordner;
};

struct Statistik {
    int kopiert = 0;
    int uebersprungen = 0;
    int konflikte = 0;
    int fehler = 0;
};

std::ofstream g_log;
bool g_mitCaches = false;

std::string U8(const fs::path& p) {
    auto s = p.u8string();
    return std::string(s.begin(), s.end());
}

void Ausgabe(const std::string& zeile) {
    std::cout << zeile << '\n';
    if (g_log) g_log << zeile << '\n';
}

// Windows unterscheidet Gross-/Kleinschreibung nicht: "Brief.docx" == "brief.DOCX".
std::string Schluessel(const fs::path& relativ) {
#ifdef _WIN32
    std::wstring w = relativ.wstring();
    for (auto& c : w) c = static_cast<wchar_t>(std::towlower(c));
    return U8(fs::path(w));
#else
    return U8(relativ);
#endif
}

std::string Klein(const fs::path& name) {
    std::string s = U8(name);
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Ordner, deren Inhalt die Programme jederzeit neu erzeugen.
bool IstCacheOrdner(const fs::path& name) {
    if (g_mitCaches) return false;
    static const char* const kCaches[] = {
        "cache", "code cache", "gpucache", "dawncache", "shadercache", "grshadercache",
        "cache_data", "cachestorage", "scriptcache",
        "coverart", "stripes", "transients",   // Traktor
    };
    const std::string n = Klein(name);
    for (const char* c : kCaches)
        if (n == c) return true;
    return false;
}

bool Ignorieren(const fs::path& name) {
    std::string n = U8(name);
    std::string klein = n;
    std::transform(klein.begin(), klein.end(), klein.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (klein == "desktop.ini" || klein == "thumbs.db") return true;
    if (n.rfind("~$", 0) == 0) return true;              // Office-Sperrdateien
    if (klein.size() > 10 && klein.compare(klein.size() - 10, 10, ".sync-temp") == 0) return true;
    return false;
}

// Junctions/Verknuepfungen wie "Eigene Musik" nicht betreten und nicht kopieren.
bool IstVerknuepfung(const fs::directory_entry& e) {
    std::error_code ec;
    if (e.is_symlink(ec)) return true;
#ifdef _WIN32
    DWORD attr = GetFileAttributesW(e.path().c_str());
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_REPARSE_POINT)) return true;
#endif
    return false;
}

void Einlesen(const fs::path& wurzel, const fs::path& relativ, Bestand& b, Statistik& st) {
    std::error_code ec;
    fs::directory_iterator it(wurzel / relativ, fs::directory_options::skip_permission_denied, ec);
    if (ec) {
        Ausgabe("  FEHLER beim Lesen von " + U8(wurzel / relativ) + ": " + ec.message());
        ++st.fehler;
        return;
    }
    for (const auto& e : it) {
        const fs::path name = e.path().filename();
        if (Ignorieren(name) || IstVerknuepfung(e)) continue;
        const fs::path rel = relativ / name;

        if (e.is_directory(ec)) {
            if (IstCacheOrdner(name)) continue;
            b.ordner[Schluessel(rel)] = rel;
            Einlesen(wurzel, rel, b, st);
        } else if (e.is_regular_file(ec)) {
            Datei d;
            d.relativ = rel;
            d.zeit = e.last_write_time(ec);
            if (!ec) d.groesse = e.file_size(ec);
            if (ec) {
                Ausgabe("  FEHLER bei " + U8(e.path()) + ": " + ec.message());
                ++st.fehler;
                continue;
            }
            b.dateien[Schluessel(rel)] = d;
        }
    }
}

// Kopiert ueber eine Temp-Datei, damit bei einem Abbruch nie eine halbe Datei
// die alte Version ersetzt. Danach wird das Original-Aenderungsdatum gesetzt.
bool Kopieren(const fs::path& von, const fs::path& nach, fs::file_time_type zeit) {
    std::error_code ec;
    fs::create_directories(nach.parent_path(), ec);
    ec.clear();
    fs::path temp = nach;
    temp += ".sync-temp";
    fs::copy_file(von, temp, fs::copy_options::overwrite_existing, ec);
    if (!ec) fs::last_write_time(temp, zeit, ec);
    if (!ec) fs::rename(temp, nach, ec);
    if (ec) {
        std::error_code ignor;
        fs::remove(temp, ignor);
        Ausgabe("  FEHLER: " + U8(nach) + ": " + ec.message());
        return false;
    }
    return true;
}

void Uebertragen(const Datei& d, const fs::path& vonWurzel, const fs::path& nachWurzel,
                 const std::string& richtung, const std::string& grund, bool probelauf,
                 Statistik& st) {
    Ausgabe("  " + richtung + "  " + U8(d.relativ) + "  (" + grund + ")");
    if (probelauf) {
        ++st.kopiert;
        return;
    }
    if (Kopieren(vonWurzel / d.relativ, nachWurzel / d.relativ, d.zeit))
        ++st.kopiert;
    else
        ++st.fehler;
}

fs::path Umgebung(const char* name) {
#ifdef _WIN32
    std::wstring wname(name, name + std::strlen(name));
    wchar_t puffer[32768];
    DWORD n = GetEnvironmentVariableW(wname.c_str(), puffer, 32768);
    if (n > 0 && n < 32768) return fs::path(std::wstring(puffer, n));
    return {};
#else
    const char* v = std::getenv(name);
    return v ? fs::path(v) : fs::path();
#endif
}

fs::path StandardC() {
    fs::path profil = Umgebung("USERPROFILE");
    if (profil.empty()) profil = Umgebung("HOME");
    return profil / "Documents";
}

fs::path StandardY() {
#ifdef _WIN32
    PWSTR pfad = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &pfad))) {
        std::wstring s = pfad;
        CoTaskMemFree(pfad);
        if (s.size() >= 2 && (s[0] == L'Y' || s[0] == L'y') && s[1] == L':') return fs::path(s);
    }
#endif
    return fs::path("Y:\\Documents");
}

fs::path ProgrammOrdner() {
#ifdef _WIN32
    wchar_t puffer[MAX_PATH * 4];
    DWORD n = GetModuleFileNameW(nullptr, puffer, MAX_PATH * 4);
    if (n > 0) return fs::path(std::wstring(puffer, n)).parent_path();
#endif
    std::error_code ec;
    return fs::current_path(ec);
}

std::string Zeitstempel(const char* format) {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::ostringstream os;
    os << std::put_time(&tm, format);
    return os.str();
}

bool IstUnterordner(const fs::path& innen, const fs::path& aussen) {
    std::string i = Schluessel(innen), a = Schluessel(aussen);
    if (!a.empty() && a.back() != '/' && a.back() != '\\') a += static_cast<char>(fs::path::preferred_separator);
    return i.rfind(a, 0) == 0;
}

// Prueft, ob jede Datei von C auf Y mindestens genauso neu vorhanden ist.
bool AllesAufY(const fs::path& ordnerC, const fs::path& ordnerY) {
    Statistik st;
    Bestand c, y;
    Einlesen(ordnerC, {}, c, st);
    Einlesen(ordnerY, {}, y, st);
    int fehlt = 0;
    for (const auto& [k, dc] : c.dateien) {
        auto iy = y.dateien.find(k);
        if (iy == y.dateien.end() || dc.zeit - iy->second.zeit > kToleranz) {
            if (++fehlt <= 20) Ausgabe("  fehlt/aelter auf Y: " + U8(dc.relativ));
        }
    }
    if (fehlt > 20) Ausgabe("  ... und " + std::to_string(fehlt - 20) + " weitere");
    return fehlt == 0 && st.fehler == 0;
}

bool JunctionAnlegen(const fs::path& link, const fs::path& ziel) {
#ifdef _WIN32
    std::wstring cmd = L"cmd.exe /c mklink /J \"" + link.wstring() + L"\" \"" + ziel.wstring() + L"\"";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi))
        return false;
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return code == 0;
#else
    std::error_code ec;
    fs::create_directory_symlink(ziel, link, ec);
    return !ec;
#endif
}

// Ersetzt den Ordner auf C durch eine Junction auf Y. Der alte Ordner bleibt
// als "..._alt" liegen, es wird nichts geloescht.
bool Umleiten(const fs::path& ordnerC, const fs::path& ordnerY) {
    Ausgabe("");
    Ausgabe("=== Umleitung einrichten ===");

    fs::path prog = ProgrammOrdner();
    if (Schluessel(prog) == Schluessel(ordnerC) || IstUnterordner(prog, ordnerC)) {
        Ausgabe("Das Programm liegt selbst im Ordner auf C (" + U8(prog) + ").");
        Ausgabe("Bitte den Kopieren-Ordner zuerst nach Y verschieben (z. B. Y:\\Tools) und dort starten.");
        return false;
    }
    std::error_code ec;
    fs::current_path(ordnerY, ec);   // damit wir den C-Ordner nicht selbst blockieren

    Ausgabe("Pruefe, ob alles auf Y angekommen ist ...");
    if (!AllesAufY(ordnerC, ordnerY)) {
        Ausgabe("Nicht alle Dateien sind auf Y - Umleitung wird NICHT eingerichtet.");
        return false;
    }
    Ausgabe("  OK, alles da.");

    fs::path alt = ordnerC;
    alt += "_alt";
    if (fs::exists(alt, ec)) {
        alt = ordnerC;
        alt += "_alt_" + Zeitstempel("%Y-%m-%d_%H-%M-%S");
    }

    Ausgabe("");
    Ausgabe("Jetzt wird:");
    Ausgabe("  1. " + U8(ordnerC) + "  umbenannt in  " + U8(alt));
    Ausgabe("  2. an seiner Stelle eine Umleitung auf  " + U8(ordnerY) + "  angelegt.");
    Ausgabe("Bitte vorher alle Programme schliessen (Traktor, Spiele, Launcher, Office ...).");
    std::cout << "Fortfahren? Tippe JA und Enter: " << std::flush;
    std::string antwort;
    std::getline(std::cin, antwort);
    std::transform(antwort.begin(), antwort.end(), antwort.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    if (antwort != "JA" && antwort != "J") {
        Ausgabe("Abgebrochen - nichts veraendert.");
        return false;
    }

    fs::rename(ordnerC, alt, ec);
    if (ec) {
        Ausgabe("FEHLER: Umbenennen hat nicht geklappt: " + ec.message());
        Ausgabe("Meist ist noch ein Programm oder ein Explorer-Fenster im Ordner offen.");
        Ausgabe("Alles schliessen (notfalls neu anmelden) und nochmal versuchen. Nichts wurde veraendert.");
        return false;
    }

    if (!JunctionAnlegen(ordnerC, ordnerY)) {
        Ausgabe("FEHLER: Umleitung konnte nicht angelegt werden - mache Umbenennung rueckgaengig.");
        std::error_code ec2;
        fs::rename(alt, ordnerC, ec2);
        if (ec2) Ausgabe("ACHTUNG: Zurueckbenennen fehlgeschlagen. Bitte " + U8(alt) +
                         " von Hand wieder in " + U8(ordnerC) + " umbenennen.");
        return false;
    }

    // Sonst zeigt der Explorer den alten Ordner auch als "Dokumente" an.
    fs::path ini = alt / "desktop.ini";
    if (fs::exists(ini, ec)) {
#ifdef _WIN32
        SetFileAttributesW(ini.c_str(), FILE_ATTRIBUTE_NORMAL);
#endif
        fs::rename(ini, alt / "desktop.ini.bak", ec);
    }

    Ausgabe("");
    Ausgabe("Fertig! " + U8(ordnerC) + " zeigt jetzt auf " + U8(ordnerY) + ".");
    Ausgabe("Der alte Inhalt liegt in " + U8(alt) + ".");
    Ausgabe("Wenn alle Programme ein paar Tage normal laufen, kannst du diesen Ordner loeschen.");
    return true;
}

void Hilfe() {
    std::cout <<
        "DokumenteSync - holt alles von C nach Y, das neuere Datum gewinnt.\n\n"
        "  --probelauf    nur anzeigen, nichts kopieren\n"
        "  --umleiten     danach C-Ordner durch eine Junction auf Y ersetzen\n"
        "  --mit-caches   auch Cache-Ordner kopieren\n"
        "  --c PFAD       Ordner auf C  (Standard: %USERPROFILE%\\Documents)\n"
        "  --y PFAD       Ordner auf Y  (Standard: eingestellter Dokumente-Ordner oder Y:\\Documents)\n";
}

int Programm(const std::vector<fs::path>& args) {
    bool probelauf = false, umleiten = false;
    fs::path ordnerC, ordnerY;

    for (size_t i = 0; i < args.size(); ++i) {
        std::string a = U8(args[i]);
        if (a == "--probelauf" || a == "-p") probelauf = true;
        else if (a == "--umleiten") umleiten = true;
        else if (a == "--mit-caches") g_mitCaches = true;
        else if (a == "--c" && i + 1 < args.size()) ordnerC = args[++i];
        else if (a == "--y" && i + 1 < args.size()) ordnerY = args[++i];
        else if (a == "--hilfe" || a == "-h" || a == "/?") { Hilfe(); return 0; }
        else { std::cout << "Unbekannte Option: " << a << "\n\n"; Hilfe(); return 2; }
    }
    if (ordnerC.empty()) ordnerC = StandardC();
    if (ordnerY.empty()) ordnerY = StandardY();

    std::error_code ec;
    if (!fs::is_directory(ordnerC, ec)) {
        std::cout << "Ordner C wurde nicht gefunden: " << U8(ordnerC) << '\n';
        return 2;
    }
    if (!fs::is_directory(ordnerY, ec)) {
        std::cout << "Ordner Y wurde nicht gefunden: " << U8(ordnerY)
                  << "\n(Laufwerk Y angeschlossen? Pfad mit --y angeben.)\n";
        return 2;
    }
    if (IstVerknuepfung(fs::directory_entry(ordnerC, ec))) {
        std::cout << "Ordner C ist bereits eine Umleitung (Junction): " << U8(ordnerC)
                  << "\nAlles, was Programme dort ablegen, landet schon auf Y. Nichts zu tun.\n";
        return 0;
    }
    ordnerC = fs::canonical(ordnerC, ec);
    ordnerY = fs::canonical(ordnerY, ec);
    if (Schluessel(ordnerC) == Schluessel(ordnerY)) {
        std::cout << "Ordner C und Y sind derselbe Ordner - nichts zu tun.\n";
        return 2;
    }
    if (IstUnterordner(ordnerC, ordnerY) || IstUnterordner(ordnerY, ordnerC)) {
        std::cout << "Ein Ordner liegt im anderen - das wuerde eine Endlosschleife erzeugen.\n";
        return 2;
    }

    fs::path logOrdner = ProgrammOrdner() / "Protokolle";
    fs::create_directories(logOrdner, ec);
    fs::path logDatei = logOrdner / ("Sync_" + Zeitstempel("%Y-%m-%d_%H-%M-%S") + ".log");
    g_log.open(logDatei, std::ios::binary);

    Ausgabe("Start    : " + Zeitstempel("%d.%m.%Y %H:%M:%S"));
    Ausgabe("Ordner C : " + U8(ordnerC));
    Ausgabe("Ordner Y : " + U8(ordnerY));
    if (probelauf) Ausgabe("*** PROBELAUF: Es wird nichts kopiert, nur angezeigt. ***");
    if (umleiten && probelauf) Ausgabe("*** --umleiten wird im Probelauf nicht ausgefuehrt. ***");
    if (!g_mitCaches) Ausgabe("Cache-Ordner werden uebersprungen (mit --mit-caches doch kopieren).");
    Ausgabe("");

    Statistik st;
    Bestand c, y;
    Ausgabe("Lese Ordner ein ...");
    Einlesen(ordnerC, {}, c, st);
    Einlesen(ordnerY, {}, y, st);
    Ausgabe("  C: " + std::to_string(c.dateien.size()) + " Dateien,  Y: " +
            std::to_string(y.dateien.size()) + " Dateien");
    Ausgabe("");

    // Auch leere Ordner von C auf Y anlegen.
    if (!probelauf)
        for (const auto& [k, rel] : c.ordner)
            if (!y.ordner.count(k)) fs::create_directories(ordnerY / rel, ec);

    Ausgabe("Abgleich C -> Y:");
    for (const auto& [k, dc] : c.dateien) {
        auto iy = y.dateien.find(k);
        if (iy == y.dateien.end()) {
            Uebertragen(dc, ordnerC, ordnerY, "C -> Y", "fehlt auf Y", probelauf, st);
            continue;
        }
        const Datei& dy = iy->second;
        const auto diff = dc.zeit - dy.zeit;
        if (diff > kToleranz) {
            Uebertragen(dc, ordnerC, ordnerY, "C -> Y", "auf C neuer", probelauf, st);
        } else if (-diff > kToleranz) {
            Ausgabe("  bleibt    " + U8(dy.relativ) + "  (auf Y neuer)");
            ++st.uebersprungen;
        } else if (dc.groesse != dy.groesse) {
            // Gleiches Datum, aber andere Groesse: nicht raten, sondern melden.
            Ausgabe("  KONFLIKT  " + U8(dc.relativ) +
                    "  (gleiches Datum, andere Groesse - bitte selbst pruefen)");
            ++st.konflikte;
        } else {
            ++st.uebersprungen;
        }
    }
    Ausgabe("");
    Ausgabe(std::string(probelauf ? "Wuerde kopieren  : " : "Kopiert          : ") + std::to_string(st.kopiert));
    Ausgabe("Y schon aktuell  : " + std::to_string(st.uebersprungen));
    Ausgabe("Konflikte        : " + std::to_string(st.konflikte));
    Ausgabe("Fehler           : " + std::to_string(st.fehler));
    Ausgabe("Protokoll        : " + U8(logDatei));
    if (st.fehler > 0) {
        if (umleiten) Ausgabe("\nEs gab Fehler - Umleitung wird NICHT eingerichtet.");
        return 1;
    }
    if (umleiten && !probelauf) return Umleiten(ordnerC, ordnerY) ? 0 : 1;
    return 0;
}

}  // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    std::vector<fs::path> args;
    for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);
    return Programm(args);
}
#else
int main(int argc, char** argv) {
    std::vector<fs::path> args;
    for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);
    return Programm(args);
}
#endif
