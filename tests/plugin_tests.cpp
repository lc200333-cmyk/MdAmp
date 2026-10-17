#include <windows.h>
#include <array>
#include <cmath>
#include <iostream>
#include "plugin_api.h"
int wmain(int count,wchar_t** arguments) {
    if (count != 2) return 1;
    const auto module = LoadLibraryExW(arguments[1],nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!module) return 2;
    const auto entry = reinterpret_cast<MdAmpGetDspV1>(GetProcAddress(module,"MdAmpGetPluginV1"));
    if (!entry) { FreeLibrary(module); return 3; }
    const auto* api = entry();
    if (!api || api->abiVersion != 1 || !api->process || !api->setAmount) { FreeLibrary(module); return 4; }
    std::array<float,4> data{{.5f,.5f,.5f,0}};
    api->setAmount(1); api->process(data.data(),2,2);
    const bool ok = std::abs(data[0] - .5f) < .0001f && std::abs(data[1] - .5f) < .0001f && data[2] > .5f && data[3] < 0;
    api->setAmount(0); std::array<float,2> flat{{.25f,-.25f}}; api->process(flat.data(),1,2);
    const bool bypass = flat[0] == .25f && flat[1] == -.25f;
    FreeLibrary(module);
    std::cout << (ok && bypass ? "DSP ABI and PCM processing passed\n" : "DSP test failed\n");
    return ok && bypass ? 0 : 5;
}
