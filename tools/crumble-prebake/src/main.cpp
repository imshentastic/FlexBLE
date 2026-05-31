// crumble-prebake CLI -- off-device EPUB cache prebake.
//
// Phase 1 scope: emit book.bin per input EPUB. Phases 2-4 (sections,
// css_rules.cache, cover thumbs) follow on the same scaffolding.

#include <Arduino.h>
#include <HalStorage.h>
#include <Logging.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace {

void usage(const char* argv0) {
  std::fprintf(stderr,
               "Usage: %s [options] <epub> [<epub> ...]\n"
               "\n"
               "Options:\n"
               "  --output-dir <dir>     Write cache state into <dir>/.crosspoint/epub_<hash>/\n"
               "                         instead of next to each input EPUB.\n"
               "  --sd-mount <path>      Alias for --output-dir; self-documents the SD-card\n"
               "                         workflow.\n"
               "  --check                Skip books whose existing book.bin is fresh against\n"
               "                         the input EPUB's mtime.\n"
               "  --verbose              Per-step timing on stderr.\n"
               "  -h, --help             Show this help.\n",
               argv0);
}

struct Options {
  std::string outputDir;
  std::vector<std::string> epubs;
  bool check = false;
  bool verbose = false;
};

bool parseArgs(int argc, char** argv, Options& out) {
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "-h" || a == "--help") {
      usage(argv[0]);
      std::exit(0);
    } else if ((a == "--output-dir" || a == "--sd-mount") && i + 1 < argc) {
      out.outputDir = argv[++i];
    } else if (a == "--check") {
      out.check = true;
    } else if (a == "--verbose") {
      out.verbose = true;
    } else if (a.rfind("--", 0) == 0) {
      std::fprintf(stderr, "Unknown option: %s\n", a.c_str());
      return false;
    } else {
      out.epubs.push_back(a);
    }
  }
  if (out.epubs.empty()) {
    std::fprintf(stderr, "Error: at least one EPUB path required.\n");
    return false;
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  Options opts;
  if (!parseArgs(argc, argv, opts)) {
    usage(argv[0]);
    return 2;
  }

  // Sanity: confirm the shim layer is wired up. This is the phase-1
  // smoke test before we start dragging in the firmware parser code.
  LOG_INF("CLI", "crumble-prebake starting (millis=%u)", millis());
  LOG_INF("CLI", "output dir: %s", opts.outputDir.empty() ? "<inputs' siblings>" : opts.outputDir.c_str());
  for (const auto& path : opts.epubs) {
    const bool present = Storage.exists(path.c_str());
    LOG_INF("CLI", "input: %s (exists=%d)", path.c_str(), present ? 1 : 0);
  }
  LOG_INF("CLI", "(phase 1 prebake of book.bin -- implementation pending)");
  return 0;
}
