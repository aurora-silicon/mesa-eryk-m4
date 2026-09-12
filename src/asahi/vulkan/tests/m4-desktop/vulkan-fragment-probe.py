#!/usr/bin/env python3
"""Build an exact-output Vulkan fragment probe; never execute a GPU workload.

Retains the input shader, generated SPIR-V header, adapted triangle fixture,
commands and hashes. Run the resulting ARM binary under gpu-experiment.py.
--host-compile-only builds a host pipeline-creation probe that cannot submit
work, for use with the M4 DRM shim. --vertex can replace its vertex shader.
"""
import argparse
import hashlib
import json
import os
import shutil
import subprocess
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--samples', type=int, choices=[1, 2, 4], default=1)
    parser.add_argument('--occlusion-query', action='store_true',
        help='Report a non-precise samples-passed query around the diagnostic draw')
    parser.add_argument('--allow-unadvertised-samples', action='store_true',
        help='Diagnostic only: attempt the requested sample count even when unadvertised')
    parser.add_argument('--rgba', type=int, nargs=4, required=True)
    parser.add_argument('--fixture', type=Path)
    parser.add_argument('--workspace-root', type=Path,
                        default=Path(__file__).resolve().parent.parent)
    parser.add_argument('--vertex', type=Path)
    parser.add_argument('--transform-feedback', action='store_true', help=
        'Capture three vec4 positions into buffer 0 and verify them plus pixels')
    parser.add_argument('--host-compile-only', action='store_true')
    parser.add_argument('--run-host-shim', action='store_true', help=
        'Build and run the host compile-only probe under the M4 no-op DRM shim')
    parser.add_argument('--record-command-buffer', action='store_true', help=
        'With --run-host-shim, also record one draw without submitting it')
    parser.add_argument('--sysroot', type=Path, default=Path(
        '/home/council/omarchy/graphics-build/sysroot'))
    args = parser.parse_args()
    args.host_compile_only |= args.run_host_shim
    if args.record_command_buffer and not args.run_host_shim:
        parser.error('--record-command-buffer requires --run-host-shim')
    if args.transform_feedback and not args.vertex:
        parser.error('--transform-feedback requires an XFB-decorated --vertex')
    for path in (args.source, args.vertex):
        if path is not None and not path.is_file():
            parser.error(f'Input does not exist: {path}')
    if any(x < 0 or x > 255 for x in args.rgba):
        parser.error('RGBA bytes must be in 0..255')

    root = args.workspace_root.resolve()
    fixture = args.fixture or root / (
        'mesa/m4-asahi/src/asahi/vulkan/tests/g17p_offscreen_triangle.c')
    fixture = fixture.resolve()
    source = fixture.read_text()
    old = 'static const uint8_t expected[4] = {255, 71, 13, 255};'
    if source.count(old) != 1:
        raise RuntimeError('Fixture oracle changed; update adapter')
    source = source.replace(old, 'static const uint8_t expected[4] = {' +
                            ', '.join(map(str, args.rgba)) + '};')
    def replace_once(old, new):
        nonlocal source
        if source.count(old) != 1:
            raise RuntimeError('Probe fixture anchor changed: ' + old)
        source = source.replace(old, new)
    if args.transform_feedback:
        functions = ('vkCmdBindTransformFeedbackBuffersEXT',
                     'vkCmdBeginTransformFeedbackEXT',
                     'vkCmdEndTransformFeedbackEXT', 'vkCmdPipelineBarrier')
        replace_once('VK_FN(vkCreateBuffer);', 'VK_FN(vkCreateBuffer);\n' +
            '\n'.join('VK_FN(' + name + ');' for name in functions))
        replace_once('   LOAD_DEVICE(device, vkCreateBuffer);',
            '   LOAD_DEVICE(device, vkCreateBuffer);\n' + '\n'.join(
                '   LOAD_DEVICE(device, ' + name + ');' for name in functions))
        replace_once('   const VkDeviceCreateInfo device_info = {', '''   const VkPhysicalDeviceTransformFeedbackFeaturesEXT xfb_features = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_FEATURES_EXT,
      .transformFeedback = VK_TRUE,
   };
   const char *xfb_extension = VK_EXT_TRANSFORM_FEEDBACK_EXTENSION_NAME;
   const VkDeviceCreateInfo device_info = {''')
        replace_once('      .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,', '''      .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .pNext = &xfb_features,
      .enabledExtensionCount = 1,
      .ppEnabledExtensionNames = &xfb_extension,''')
        replace_once('   memcpy(vertex_map, vertices, sizeof(vertices));', '''   memcpy(vertex_map, vertices, sizeof(vertices));
   const VkDeviceSize xfb_size = 64, xfb_offset = 0;
   VkBufferCreateInfo xfb_info = vertex_info;
   xfb_info.size = xfb_size;
   xfb_info.usage = VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_BUFFER_BIT_EXT;
   VkBuffer xfb_buffer;
   check(vkCreateBuffer(device, &xfb_info, NULL, &xfb_buffer), "vkCreateBuffer(xfb)");
   VkMemoryRequirements xfb_req;
   vkGetBufferMemoryRequirements(device, xfb_buffer, &xfb_req);
   VkMemoryAllocateInfo xfb_alloc = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .allocationSize = xfb_req.size,
      .memoryTypeIndex = find_memory_type(&memory_properties, xfb_req.memoryTypeBits,
         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
   };
   VkDeviceMemory xfb_memory;
   check(vkAllocateMemory(device, &xfb_alloc, NULL, &xfb_memory), "vkAllocateMemory(xfb)");
   check(vkBindBufferMemory(device, xfb_buffer, xfb_memory, 0), "vkBindBufferMemory(xfb)");
   void *xfb_map;
   check(vkMapMemory(device, xfb_memory, 0, xfb_size, 0, &xfb_map), "vkMapMemory(xfb)");
   memset(xfb_map, 0xcd, xfb_size);''')
        replace_once('   vkCmdDraw(command_buffer, 3, 1, 0, 0);', '''   vkCmdBindTransformFeedbackBuffersEXT(command_buffer, 0, 1, &xfb_buffer,
                                        &xfb_offset, &xfb_size);
   vkCmdBeginTransformFeedbackEXT(command_buffer, 0, 0, NULL, NULL);
   vkCmdDraw(command_buffer, 3, 1, 0, 0);
   vkCmdEndTransformFeedbackEXT(command_buffer, 0, 0, NULL, NULL);''')
        replace_once('   vkCmdEndRenderPass(command_buffer);', '''   vkCmdEndRenderPass(command_buffer);
   const VkMemoryBarrier xfb_host = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
      .srcAccessMask = VK_ACCESS_TRANSFORM_FEEDBACK_WRITE_BIT_EXT,
      .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
   };
   vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFORM_FEEDBACK_BIT_EXT,
      VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &xfb_host, 0, NULL, 0, NULL);''')
        replace_once('   printf(PROBE_NAME ": retired %u submissions\\n", repeats);', '''   static const float xfb_expected[3][4] = {
      {-1, -1, 0, 1}, {3, -1, 0, 1}, {-1, 3, 0, 1},
   };
   bool xfb_failed = memcmp(xfb_map, xfb_expected, sizeof(xfb_expected)) != 0;
   if (xfb_failed) {
      const float *f = xfb_map;
      for (unsigned i = 0; i < 3; ++i)
         fprintf(stderr, "XFB[%u] = %g %g %g %g\\n", i,
                 f[4*i], f[4*i+1], f[4*i+2], f[4*i+3]);
      fprintf(stderr, "FAIL: transform feedback vertex mismatch\\n");
   }
   for (unsigned i = sizeof(xfb_expected); i < xfb_size; ++i)
      if (((unsigned char *)xfb_map)[i] != 0xcd) {
         fprintf(stderr, "FAIL: transform feedback guard byte %u overwritten\\n", i);
         xfb_failed = true;
      }
   if (!xfb_failed)
      puts("PASS: transform feedback three exact vec4 positions and buffer guard");
   printf(PROBE_NAME ": retired %u submissions\\n", repeats);''')
        # Finish the pixel oracle even if XFB is wrong, but preserve failure.
        end = source.rfind('   return EXIT_SUCCESS;')
        source = source[:end] + source[end:].replace('   return EXIT_SUCCESS;',
            '   return xfb_failed ? EXIT_FAILURE : EXIT_SUCCESS;', 1)
        replace_once('vkWaitForFences(device, 1, &fence, true, UINT64_MAX)',
                     'vkWaitForFences(device, 1, &fence, true, UINT64_C(3000000000))')
    if args.occlusion_query:
        query_functions = ('vkCreateQueryPool', 'vkCmdResetQueryPool',
                           'vkCmdBeginQuery', 'vkCmdEndQuery',
                           'vkGetQueryPoolResults')
        replace_once('VK_FN(vkCreateBuffer);', 'VK_FN(vkCreateBuffer);\n' +
            '\n'.join('VK_FN(' + name + ');' for name in query_functions))
        replace_once('   LOAD_DEVICE(device, vkCreateBuffer);',
            '   LOAD_DEVICE(device, vkCreateBuffer);\n' + '\n'.join(
                '   LOAD_DEVICE(device, ' + name + ');' for name in query_functions))
        replace_once('   VkCommandBuffer command_buffer;', '''   VkQueryPool visibility_query;
   const VkQueryPoolCreateInfo visibility_info = {
      .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
      .queryType = VK_QUERY_TYPE_OCCLUSION,
      .queryCount = 1,
   };
   check(vkCreateQueryPool(device, &visibility_info, NULL, &visibility_query),
         "vkCreateQueryPool(visibility)");
   VkCommandBuffer command_buffer;''')
        replace_once('   vkCmdBeginRenderPass(command_buffer, &render_begin,',
            '   vkCmdResetQueryPool(command_buffer, visibility_query, 0, 1);\n'
            '   vkCmdBeginRenderPass(command_buffer, &render_begin,')
        replace_once('   vkCmdDraw(command_buffer, 3, 1, 0, 0);',
            '   vkCmdBeginQuery(command_buffer, visibility_query, 0, 0);\n'
            '   vkCmdDraw(command_buffer, 3, 1, 0, 0);\n'
            '   vkCmdEndQuery(command_buffer, visibility_query, 0);')
        replace_once('   printf(PROBE_NAME ": retired %u submissions\\n", repeats);',
            '''   uint64_t visibility_samples = UINT64_MAX;
   check(vkGetQueryPoolResults(device, visibility_query, 0, 1,
         sizeof(visibility_samples), &visibility_samples, sizeof(visibility_samples),
         VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT),
         "vkGetQueryPoolResults(visibility)");
   printf("SAMPLES-PASSED non-precise: %llu\\n",
          (unsigned long long)visibility_samples);
   printf(PROBE_NAME ": retired %u submissions\\n", repeats);''')
    if args.allow_unadvertised_samples:
        check = '      die("requested color sample count is not advertised");'
        if source.count(check) != 1:
            raise RuntimeError('Sample capability check changed; update adapter')
        source = source.replace(check,
            '      fprintf(stderr, "DIAGNOSTIC: sample count is unadvertised; '
            'this is not Vulkan capability qualification\\n");')
    if args.record_command_buffer:
        marker = '   const bool record_only ='
        if source.count(marker) != 1:
            raise RuntimeError('Recording fixture changed; update adapter')
        source = source.replace(marker, marker + ' true ||')
        source = source.replace('feature advertised, shaders compiled, ',
            'host shim shaders compiled; no GPU submission, ')
    elif args.host_compile_only:
        marker = '         "vkCreateGraphicsPipelines");'
        if source.count(marker) != 1:
            raise RuntimeError('Pipeline fixture changed; update adapter')
        source = source.replace(marker, marker + '\n'
            '   puts("PASS: pipeline compilation only; no GPU submission");\n'
            '   return EXIT_SUCCESS;\n')
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    shutil.copyfile(args.source, output / 'probe.frag')
    (output / 'probe.c').write_text(source)
    commands = [
        ['glslangValidator', '-V', '--target-env', 'vulkan1.1', '--vn',
         'g17p_wsi_triangle_frag_spv', '-o',
         str(output / 'g17p_wsi_triangle_frag.h'), str(output / 'probe.frag')],
    ]
    if args.vertex:
        shutil.copyfile(args.vertex, output / 'probe.vert')
        commands.append([
            'glslangValidator', '-V', '--target-env', 'vulkan1.1', '--vn',
            'g17p_wsi_triangle_vert_spv', '-o',
            str(output / 'g17p_wsi_triangle_vert.h'), str(output / 'probe.vert')])
    target_flags = [] if args.host_compile_only else [
         '--target=aarch64-linux-gnu', '--sysroot=' + str(args.sysroot),
         '--gcc-install-dir=' + str(args.sysroot /
             'usr/lib/gcc/aarch64-unknown-linux-gnu/16.1.1'), '-fuse-ld=lld']
    binary = output / ('probe-host-compile-only' if args.host_compile_only
                       else 'probe-arm64')
    commands.append(['clang', *target_flags,
         '-DTEST_DEVICE_ID=0x8132', '-DG17P_MSAA_SAMPLES=' + str(args.samples),
         '-I' + str(fixture.parent), str(output / 'probe.c'), '-o',
         str(binary), '-ldl'])
    with (output / 'build.log').open('w') as log:
        for command in commands:
            subprocess.run(command, stdout=log, stderr=subprocess.STDOUT,
                           check=True, timeout=60)
    files = {f.name: hashlib.sha256(f.read_bytes()).hexdigest()
             for f in output.iterdir() if f.is_file()}
    manifest = {
        'samples': args.samples, 'rgba': args.rgba,
        'occlusion_query': args.occlusion_query,
        'transform_feedback': args.transform_feedback,
        'allow_unadvertised_samples': args.allow_unadvertised_samples,
        'host_compile_only': args.host_compile_only,
        'record_command_buffer': args.record_command_buffer,
        'fixture_sha256': hashlib.sha256(fixture.read_bytes()).hexdigest(),
        'commands': commands, 'sha256': files,
        'limitation': 'Adapted diagnostic fixture; not an application capture '
                      'or hardware qualification.',
    }
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    if args.run_host_shim:
        host = root / 'build/mesa-m4-asahi-host'
        shim = host / 'src/asahi/drm-shim/libasahi_noop_drm_shim.so'
        icd = host / 'src/asahi/vulkan/asahi_devenv_icd.x86_64.json'
        library = host / 'src/asahi/vulkan/libvulkan_asahi.so'
        captures = output / 'compiler'
        captures.mkdir()
        env = {k: v for k, v in os.environ.items() if not k.startswith(
            ('VK_', 'MESA_', 'AGX_', 'ASAHI_', 'HK_', 'LD_', 'GFXRECON_'))}
        overrides = dict(ASAHI_SHIM_CHIP='g16g', ASAHI_M4_EXPERIMENTAL='1',
            MESA_SHADER_CACHE_DISABLE='true', LD_PRELOAD=str(shim),
            VK_DRIVER_FILES=str(icd), AGX_COMPILER_CAPTURE_DIR=str(captures),
            AGX_COMPILER_CAPTURE_SUCCESS='all')
        env.update(overrides)
        manifest['host_environment'] = overrides
        manifest['host_sha256'] = {str(f): hashlib.sha256(f.read_bytes()).hexdigest()
                                  for f in (shim, icd, library)}
        manifest['limitation'] += ' Host shim performs no GPU execution; '
        manifest['limitation'] += 'unsupported input capabilities are diagnostic only.'
        (output / 'mesa.patch').write_bytes(subprocess.check_output([
            'git', '-C', str(fixture.parents[4]), 'diff', '--binary', 'HEAD']))
        try:
            with (output / 'compile.log').open('w') as log:
                result = subprocess.run([str(binary)], env=env, stdout=log,
                    stderr=subprocess.STDOUT, timeout=20)
            manifest['returncode'] = result.returncode
            print((output / 'compile.log').read_text()[-4000:])
            result.check_returncode()
        finally:
            (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(binary)


if __name__ == '__main__':
    main()
