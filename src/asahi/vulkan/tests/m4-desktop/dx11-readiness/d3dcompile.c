/* Offline HLSL -> DXBC helper. Build with MinGW and run under Wine; no GPU. */
#define COBJMACROS
#include <windows.h>
#include <d3dcompiler.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
   if (argc != 5) {
      fprintf(stderr, "usage: d3dcompile INPUT ENTRY PROFILE OUTPUT\n");
      return 2;
   }
   FILE *f = fopen(argv[1], "rb");
   if (!f) return 2;
   fseek(f, 0, SEEK_END);
   long size = ftell(f);
   rewind(f);
   if (size <= 0) return 2;
   char *source = malloc(size);
   if (!source || fread(source, 1, size, f) != (size_t)size) return 2;
   fclose(f);
   ID3DBlob *code = NULL, *errors = NULL;
   HRESULT hr = D3DCompile(source, size, argv[1], NULL, NULL, argv[2], argv[3],
                          D3DCOMPILE_ENABLE_STRICTNESS, 0, &code, &errors);
   free(source);
   if (errors) {
      fwrite(ID3D10Blob_GetBufferPointer(errors), 1,
             ID3D10Blob_GetBufferSize(errors), stderr);
      ID3D10Blob_Release(errors);
   }
   if (FAILED(hr)) return 1;
   f = fopen(argv[4], "wb");
   if (!f) return 2;
   size_t bytes = ID3D10Blob_GetBufferSize(code);
   int ok = fwrite(ID3D10Blob_GetBufferPointer(code), 1, bytes, f) == bytes;
   ok &= fclose(f) == 0;
   ID3D10Blob_Release(code);
   return ok ? 0 : 2;
}
