// DokumenteSync - holt alles aus dem Dokumente-Ordner auf C nach Y.
//
//  - Fehlt eine Datei auf Y, wird sie von C nach Y kopiert.
//  - Gibt es sie auf beiden Seiten, gewinnt das neuere Aenderungsdatum:
//    ist C neuer, wird Y ueberschrieben; ist Y neuer, bleibt Y wie es ist.
//  - Identische Dateien (Datum gleich, +/- 2 Sekunden) werden uebersprungen.
//  - C wird nie veraendert, und es wird NIE etwas geloescht.
//
// Aufruf:
//   DokumenteSync.exe [--probelauf] [--c PFAD] [--y PFAD]
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

void Hilfe() {
    std::cout <<
        "DokumenteSync - holt alles von C nach Y, das neuere Datum gewinnt.\n\n"
        "  --probelauf    nur anzeigen, nichts kopieren\n"
        "  --c PFAD       Ordner auf C  (Standard: %USERPROFILE%\\Documents)\n"
        "  --y PFAD       Ordner auf Y  (Standard: eingestellter Dokumente-Ordner oder Y:\\Documents)\n";
}

int Programm(const std::vector<fs::path>& args) {
    bool probelauf = false;
    fs::path ordnerC, ordnerY;

    for (size_t i = 0; i < args.size(); ++i) {
        std::string a = U8(args[i]);
        if (a == "--probelauf" || a == "-p") probelauf = true;
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
    return st.fehler > 0 ? 1 : 0;
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
