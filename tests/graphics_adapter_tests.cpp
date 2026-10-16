#include <cstdio>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <windows.h>

#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#endif

#include "check.h"
#include "graphics_adapters.h"

using namespace crackdown;

namespace {

void TestValues() {
  CHECK(!ParseGraphicsAdapter("auto"));
  CHECK(!ParseGraphicsAdapter(""));
  CHECK(!ParseGraphicsAdapter("-1"));
  CHECK(!ParseGraphicsAdapter("1x"));
  CHECK(!ParseGraphicsAdapter("Intel"));
  CHECK(ParseGraphicsAdapter("0") == 0);
  CHECK(ParseGraphicsAdapter("12") == 12);

  const std::vector<GraphicsAdapter> adapters = {{0, "NVIDIA GeForce RTX 4070"}, {1, "Intel UHD Graphics 730"}};
  CHECK(ResolveGraphicsAdapter("auto", adapters) == -1);
  CHECK(ResolveGraphicsAdapter("0", adapters) == 0);
  CHECK(ResolveGraphicsAdapter("1", adapters) == 1);
  CHECK(ResolveGraphicsAdapter("2", adapters) == -1);
  CHECK(ResolveGraphicsAdapter("1", {}) == -1);
  // The index is the DXGI one, not the position in the list.
  const std::vector<GraphicsAdapter> gap = {{1, "A"}, {3, "B"}};
  CHECK(ResolveGraphicsAdapter("3", gap) == 3);
  CHECK(ResolveGraphicsAdapter("0", gap) == -1);

  auto options = GraphicsAdapterOptions(adapters, "auto");
  CHECK(options.size() == 3);
  CHECK(options[0].first == "auto" && options[0].second == "Automatic (Recommended)");
  CHECK(options[1].first == "0" && options[1].second == "NVIDIA GeForce RTX 4070");
  options = GraphicsAdapterOptions({{0, "Same"}, {1, "Same"}}, "auto");
  CHECK(options[1].second == "Same (0)" && options[2].second == "Same (1)");
  options = GraphicsAdapterOptions(adapters, "5");
  CHECK(options.size() == 4 && options[3].first == "5" && options[3].second == "Graphics card 5 (not found)");
}

#if defined(_WIN32)
// ReXGlue opens d3d12_adapter = N as the Nth adapter of EnumAdapters1.
void TestEnumerationMatchesRuntime() {
  const std::vector<GraphicsAdapter> adapters = ListGraphicsAdapters();
  for (const GraphicsAdapter& adapter : adapters) {
    std::printf("  %d  %s\n", adapter.index, adapter.name.c_str());
  }
  CHECK(!adapters.empty());
  Microsoft::WRL::ComPtr<IDXGIFactory2> factory;
  CHECK(SUCCEEDED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory))));
  if (!factory) {
    return;
  }
  for (size_t i = 0; i < adapters.size(); ++i) {
    CHECK(i == 0 || adapters[i].index > adapters[i - 1].index);
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    CHECK(factory->EnumAdapters1(UINT(adapters[i].index), &adapter) == S_OK);
    DXGI_ADAPTER_DESC1 desc;
    CHECK(adapter && SUCCEEDED(adapter->GetDesc1(&desc)) && !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE));
  }
}
#endif

}  // namespace

int main() {
  TestValues();
#if defined(_WIN32)
  TestEnumerationMatchesRuntime();
#endif
  return check::Finish("graphics_adapter_tests");
}
