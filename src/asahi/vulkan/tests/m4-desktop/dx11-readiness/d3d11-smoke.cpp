/* Native DXVK D3D11 readback oracle. Run on the M4 only under gpu-experiment.py.
 * No window or presentation: this qualifies the D3D11 translation path only. */
#include <d3d11.h>
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cwchar>
#include <vector>

static void check(HRESULT hr, const char *what) {
   if (FAILED(hr)) {
      std::fprintf(stderr, "FAIL: %s: 0x%08x\n", what, unsigned(hr));
      std::exit(1);
   }
}
#define CHECK(expr) check((expr), #expr)

static std::vector<unsigned char> read(const char *name) {
   FILE *f = std::fopen(name, "rb");
   if (!f) { std::perror(name); std::exit(2); }
   std::fseek(f, 0, SEEK_END);
   long size = std::ftell(f);
   if (size <= 0) std::exit(2);
   std::rewind(f);
   std::vector<unsigned char> data(size);
   if (std::fread(data.data(), 1, size, f) != size_t(size)) std::exit(2);
   std::fclose(f);
   return data;
}

int main(int argc, char **argv) {
   if (argc != 5 && argc != 6) {
      std::fprintf(stderr, "usage: d3d11-smoke {m4|reference} VS.dxbc PS.dxbc CS.dxbc [--present]\n");
      return 2;
   }
   bool present = argc == 6 && !std::strcmp(argv[5], "--present");
   if (argc == 6 && !present) return 2;
   bool reference = !std::strcmp(argv[1], "reference");
   if (!reference && std::strcmp(argv[1], "m4")) return 2;
   IDXGIFactory1 *factory = nullptr;
   CHECK(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void **>(&factory)));
   IDXGIAdapter1 *adapter = nullptr;
   CHECK(factory->EnumAdapters1(0, &adapter));
   DXGI_ADAPTER_DESC1 adapterDesc{};
   CHECK(adapter->GetDesc1(&adapterDesc));
   std::printf("Adapter vendor=%04x device=%04x\n", adapterDesc.VendorId, adapterDesc.DeviceId);
   if ((!reference && (adapterDesc.DeviceId != 0x8132 ||
                       !std::wcsstr(adapterDesc.Description, L"Apple M4"))) ||
       (reference && (adapterDesc.VendorId != 0x10005 ||
                      !std::wcsstr(adapterDesc.Description, L"llvmpipe")))) {
      std::fprintf(stderr, "FAIL: unexpected adapter; refusing an ambiguous renderer\n");
      return 1;
   }
   const D3D_FEATURE_LEVEL requested = D3D_FEATURE_LEVEL_11_1;
   D3D_FEATURE_LEVEL actual{};
   ID3D11Device *device = nullptr;
   ID3D11DeviceContext *ctx = nullptr;
   CHECK(D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0,
         &requested, 1, D3D11_SDK_VERSION, &device, &actual, &ctx));
   if (actual != requested) return 1;
   std::printf("PASS: D3D11 feature level 11_1 device creation\n");

   const UINT width = 64, height = 64;
   D3D11_TEXTURE2D_DESC desc{};
   desc.Width = width; desc.Height = height; desc.MipLevels = 1; desc.ArraySize = 1;
   desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
   desc.SampleDesc.Count = 1;
   desc.BindFlags = D3D11_BIND_RENDER_TARGET;
   ID3D11Texture2D *target = nullptr, *staging = nullptr;
   CHECK(device->CreateTexture2D(&desc, nullptr, &target));
   desc.BindFlags = 0; desc.Usage = D3D11_USAGE_STAGING; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
   CHECK(device->CreateTexture2D(&desc, nullptr, &staging));
   ID3D11RenderTargetView *rtv = nullptr;
   CHECK(device->CreateRenderTargetView(target, nullptr, &rtv));
   const float clear[4] = {0, 0, 1, 1};
   ctx->ClearRenderTargetView(rtv, clear);
   ctx->CopyResource(staging, target);
   D3D11_MAPPED_SUBRESOURCE map{};
   CHECK(ctx->Map(staging, 0, D3D11_MAP_READ, 0, &map));
   unsigned errors = 0;
   for (UINT y = 0; y < height; ++y)
      for (UINT x = 0; x < width; ++x) {
         auto p = static_cast<unsigned char *>(map.pData) + y * map.RowPitch + 4*x;
         errors += p[0] != 0 || p[1] != 0 || p[2] != 255 || p[3] != 255;
      }
   ctx->Unmap(staging, 0);
   if (errors) { std::printf("FAIL: clear readback: %u pixels\n", errors); return 1; }
   std::printf("PASS: D3D11 clear/copy/map 4096 pixels\n");

   auto vsCode = read(argv[2]), psCode = read(argv[3]), csCode = read(argv[4]);
   ID3D11VertexShader *vs = nullptr;
   ID3D11PixelShader *ps = nullptr;
   ID3D11ComputeShader *cs = nullptr;
   CHECK(device->CreateVertexShader(vsCode.data(), vsCode.size(), nullptr, &vs));
   CHECK(device->CreatePixelShader(psCode.data(), psCode.size(), nullptr, &ps));
   CHECK(device->CreateComputeShader(csCode.data(), csCode.size(), nullptr, &cs));
   ctx->VSSetShader(vs, nullptr, 0);
   ctx->PSSetShader(ps, nullptr, 0);
   ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
   D3D11_RASTERIZER_DESC raster{};
   raster.FillMode = D3D11_FILL_SOLID; raster.CullMode = D3D11_CULL_NONE; raster.DepthClipEnable = TRUE;
   ID3D11RasterizerState *rs = nullptr;
   CHECK(device->CreateRasterizerState(&raster, &rs));
   ctx->RSSetState(rs);
   D3D11_VIEWPORT vp{0, 0, float(width), float(height), 0, 1};
   ctx->RSSetViewports(1, &vp);
   ctx->OMSetRenderTargets(1, &rtv, nullptr);
   ctx->Draw(3, 0);
   ctx->CopyResource(staging, target);
   CHECK(ctx->Map(staging, 0, D3D11_MAP_READ, 0, &map));
   for (UINT y = 0; y < height; ++y)
      for (UINT x = 0; x < width; ++x) {
         auto p = static_cast<unsigned char *>(map.pData) + y * map.RowPitch + 4*x;
         int expected[4] = {int(std::lround((x + 0.5) * 255 / width)),
                            int(std::lround((y + 0.5) * 255 / height)), 64, 255};
         for (unsigned c = 0; c < 4; ++c)
            if (std::abs(int(p[c]) - expected[c]) > 1) {
               if (errors < 8) std::printf("pixel %u,%u channel %u: %u expected %d\n", x, y, c, p[c], expected[c]);
               ++errors;
            }
      }
   ctx->Unmap(staging, 0);
   if (errors) { std::printf("FAIL: triangle readback: %u channels\n", errors); return 1; }
   std::printf("PASS: D3D11 DXBC triangle/interpolation 4096 pixels\n");

   D3D11_BUFFER_DESC bufferDesc{};
   bufferDesc.ByteWidth = 256*4; bufferDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
   bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED; bufferDesc.StructureByteStride = 4;
   ID3D11Buffer *buffer = nullptr, *readback = nullptr;
   CHECK(device->CreateBuffer(&bufferDesc, nullptr, &buffer));
   bufferDesc.BindFlags = 0; bufferDesc.MiscFlags = 0; bufferDesc.StructureByteStride = 0;
   bufferDesc.Usage = D3D11_USAGE_STAGING; bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
   CHECK(device->CreateBuffer(&bufferDesc, nullptr, &readback));
   ID3D11UnorderedAccessView *uav = nullptr;
   CHECK(device->CreateUnorderedAccessView(buffer, nullptr, &uav));
   ctx->CSSetShader(cs, nullptr, 0);
   ctx->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
   ctx->Dispatch(4, 1, 1);
   ctx->CopyResource(readback, buffer);
   CHECK(ctx->Map(readback, 0, D3D11_MAP_READ, 0, &map));
   for (unsigned i = 0; i < 256; ++i) {
      unsigned value = static_cast<unsigned *>(map.pData)[i], expected = (i*17u)^0x12345678u;
      if (value != expected) {
         if (errors < 8) std::printf("compute %u: %08x expected %08x\n", i, value, expected);
         ++errors;
      }
   }
   ctx->Unmap(readback, 0);
   if (errors) { std::printf("FAIL: compute readback: %u words\n", errors); return 1; }
   std::printf("PASS: D3D11 DXBC compute/UAV 256 words\n");

   if (present) {
      SDL_Window *window = SDL_CreateWindow("M4 D3D11 qualification", width, height, SDL_WINDOW_VULKAN);
      if (!window) { std::fprintf(stderr, "FAIL: SDL_CreateWindow: %s\n", SDL_GetError()); return 1; }
      DXGI_SWAP_CHAIN_DESC swapDesc{};
      swapDesc.BufferDesc.Width = width; swapDesc.BufferDesc.Height = height;
      swapDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
      swapDesc.SampleDesc.Count = 1; swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
      swapDesc.BufferCount = 2; swapDesc.OutputWindow = reinterpret_cast<HWND>(window);
      swapDesc.Windowed = TRUE; swapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
      IDXGISwapChain *swap = nullptr;
      CHECK(factory->CreateSwapChain(device, &swapDesc, &swap));
      for (unsigned frame = 0; frame < 3; ++frame) {
         ID3D11Texture2D *back = nullptr;
         ID3D11RenderTargetView *backView = nullptr;
         CHECK(swap->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void **>(&back)));
         CHECK(device->CreateRenderTargetView(back, nullptr, &backView));
         ctx->OMSetRenderTargets(1, &backView, nullptr);
         ctx->Draw(3, 0);
         ctx->OMSetRenderTargets(0, nullptr, nullptr);
         CHECK(swap->Present(1, 0));
         backView->Release(); back->Release();
         SDL_PumpEvents();
      }
      ctx->Flush();
      swap->Release(); SDL_DestroyWindow(window);
      std::printf("PASS: D3D11 DXGI swapchain three FIFO presents\n");
   }
   ctx->ClearState(); ctx->Flush();
   uav->Release(); readback->Release(); buffer->Release(); rs->Release();
   cs->Release(); ps->Release(); vs->Release(); rtv->Release(); staging->Release(); target->Release();
   ctx->Release(); device->Release(); adapter->Release(); factory->Release();
   std::printf("PASS: D3D11 native DXVK smoke complete\n");
   return 0;
}
