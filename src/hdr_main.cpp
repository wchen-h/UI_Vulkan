#include "hdr_app.h"

int main() {
    try {
        HDRApp app(ASSET_DIR);
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "[FATAL] " << e.what() << std::endl;
        return 1;
    }
    return 0;
}