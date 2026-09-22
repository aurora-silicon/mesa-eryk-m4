# M3 integer multisample textures (2026-09-22)

Eight ES31 textureSize cases failed in TexStorage*Multisample with
GL_INVALID_OPERATION, for signed/unsigned 2D and array textures requesting
one or four samples. Format queries advertised multisampled R8I/R8UI
attachments, but sampledImageIntegerSampleCounts remained single-sample.
Zink rejected sampling these textures. The state tracker promotes a requested
one-sample multisample texture to at least two samples on real-MSAA drivers,
so even the one-sample request failed allocation.

Expose the existing native G15/G16 integer 2x/4x sampling and integer color
attachment counts consistently. Storage-image and no-attachment sample
limits are unchanged. This changes G15S capability reporting without adding
ES3.2 features or altering test expectations.

All eight original cases pass. The 169-case textureSize/multisample regression
has 85 Pass and 84 NotSupported (unsupported sample counts). It includes
16 passing signed/unsigned texture-use tests: 2D and arrays, requested sample
counts 1/2/3/4, actual 2x/4x storage. These draw integer values and fetch samples
in a shader for image verification; allocation/textureSize alone was not used
as proof of sampling support. Color and depth neighbors also pass.

Each segment used the unchanged 45-second M3 guard, 60-second watchdog and
five-channel 0.5-second thermal monitor with a 95 C stop. No GPU faults or
reboot occurred. Full-suite validation remains in progress.

## Passing evidence

- `build/m3-gles31-20260922/triage-msaa-integer/exact/run`: {'Pass': 8}, peak 49.25 C; results SHA256 `fa516b1423ed36dcbd9bc5fc5a8f140b877f70d626ac9c5f023b0be916ea636f`.
- `build/m3-gles31-20260922/triage-msaa-integer/neighbors-00/run`: {'Pass': 64}, peak 49.75 C; results SHA256 `bf9c146c345a2fc2a3726de98d52ef5e143e4fc9b02f9317d77861bfe4f7b33e`.
- `build/m3-gles31-20260922/triage-msaa-integer/neighbors-01/run`: {'Pass': 4, 'NotSupported': 60}, peak 49.50 C; results SHA256 `de35cf1bac4dad15da99e11f5ae78c3c893976ac3a72e8dbb0db9bc969e0341a`.
- `build/m3-gles31-20260922/triage-msaa-integer/neighbors-02/run`: {'NotSupported': 24, 'Pass': 17}, peak 49.50 C; results SHA256 `f26651428cec13ebbc814d4da43b469b135a588b6151bd123f02aba846c1a64b`.
