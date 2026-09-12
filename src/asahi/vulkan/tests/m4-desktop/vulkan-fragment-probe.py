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
    parser.add_argument('--format', choices=['rgba8', 'srgb8'], default='rgba8')
    parser.add_argument('--samples', type=int, choices=[1, 2, 4], default=1)
    parser.add_argument('--occlusion-query', action='store_true',
        help='Report a non-precise samples-passed query around the diagnostic draw')
    parser.add_argument('--allow-unadvertised-samples', action='store_true',
        help='Diagnostic only: attempt the requested sample count even when unadvertised')
    parser.add_argument('--rgba', type=int, nargs=4, required=True)
    parser.add_argument('--right-rgba', type=int, nargs=4,
        help='Expected RGBA for the right half; --rgba describes the left half')
    parser.add_argument('--fixture', type=Path)
    parser.add_argument('--workspace-root', type=Path,
                        default=Path(__file__).resolve().parent.parent)
    parser.add_argument('--vertex', type=Path)
    parser.add_argument('--geometry', type=Path)
    parser.add_argument('--indexed', action='store_true')
    parser.add_argument('--front-face', choices=['cw', 'ccw'], default='ccw')
    parser.add_argument('--extent', type=int, nargs=2, default=[32, 32])
    parser.add_argument('--storage-stage', choices=['vertex', 'fragment', 'geometry'])
    parser.add_argument('--storage-expected', type=Path,
        help='Exact little-endian uint32 output; storage starts at zero, plus 64 guard bytes')
    parser.add_argument('--reference-host', action='store_true',
        help='Build an executing host oracle restricted to the lavapipe device')
    parser.add_argument('--transform-feedback', action='store_true', help=
        'Capture three vec4 positions into buffer 0 and verify them plus pixels')
    parser.add_argument('--transform-feedback-resume', action='store_true', help=
        'End to a counter buffer, resume, and verify six vertices and a 96-byte counter')
    parser.add_argument('--rasterizer-discard', action='store_true', help=
        'Disable rasterization; the caller must supply the unchanged clear RGBA oracle')
    parser.add_argument('--host-compile-only', action='store_true')
    parser.add_argument('--run-host-shim', action='store_true', help=
        'Build and run the host compile-only probe under the M4 no-op DRM shim')
    parser.add_argument('--record-command-buffer', action='store_true', help=
        'With --run-host-shim, also record one draw without submitting it')
    parser.add_argument('--sysroot', type=Path, default=Path(
        '/home/council/omarchy/graphics-build/sysroot'))
    args = parser.parse_args()
    args.host_compile_only |= args.run_host_shim
    if args.reference_host and args.host_compile_only:
        parser.error('--reference-host conflicts with compile-only')
    if bool(args.storage_stage) != bool(args.storage_expected):
        parser.error('--storage-stage and --storage-expected are required together')
    if args.storage_stage and args.transform_feedback:
        parser.error('storage and XFB probes must be separate')
    if args.record_command_buffer and not args.run_host_shim:
        parser.error('--record-command-buffer requires --run-host-shim')
    if args.transform_feedback and not args.vertex:
        parser.error('--transform-feedback requires an XFB-decorated --vertex')
    if args.transform_feedback_resume and not args.transform_feedback:
        parser.error('--transform-feedback-resume requires --transform-feedback')
    for path in (args.source, args.vertex, args.geometry, args.storage_expected):
        if path is not None and not path.is_file():
            parser.error(f'Input does not exist: {path}')
    if any(x < 0 or x > 255 for x in args.rgba + (args.right_rgba or [])):
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
    if any(x < 1 or x > 512 for x in args.extent):
        parser.error('extent must be between 1 and 512')
    replace_once('#define IMAGE_WIDTH 32', '#define IMAGE_WIDTH ' + str(args.extent[0]))
    replace_once('#define IMAGE_HEIGHT 32', '#define IMAGE_HEIGHT ' + str(args.extent[1]))
    if args.geometry or args.storage_stage:
        features = []
        if args.geometry:
            features.append('.geometryShader = VK_TRUE')
        if args.storage_stage:
            features.append(('.fragmentStoresAndAtomics' if args.storage_stage == 'fragment'
                             else '.vertexPipelineStoresAndAtomics') + ' = VK_TRUE')
        replace_once('   const VkDeviceCreateInfo device_info = {',
            '   const VkPhysicalDeviceFeatures test_features = {' + ', '.join(features) + '};\n'
            '   const VkDeviceCreateInfo device_info = {\n      .pEnabledFeatures = &test_features,')
    if args.geometry:
        replace_once('   VkShaderModule vert_module, frag_module;',
            '   VkShaderModule geom_module;\n'
            '   const VkShaderModuleCreateInfo geom_info = {\n'
            '      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,\n'
            '      .codeSize = sizeof(probe_geom_spv), .pCode = probe_geom_spv};\n'
            '   check(vkCreateShaderModule(device, &geom_info, NULL, &geom_module), "geometry module");\n'
            '   VkShaderModule vert_module, frag_module;')
        replace_once('   const VkPipelineShaderStageCreateInfo stages[] = {',
            '   const VkPipelineShaderStageCreateInfo stages[] = {\n'
            '      {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,\n'
            '       .stage = VK_SHADER_STAGE_GEOMETRY_BIT, .module = geom_module, .pName = "main"},')
        replace_once('      .stageCount = 2,', '      .stageCount = 3,')
        source = '#include <stdint.h>\n#include "probe_geom.h"\n' + source
    if args.storage_stage:
        expected = args.storage_expected.read_bytes()
        if not expected or len(expected) % 4:
            parser.error('storage expected data must contain uint32 words')
        words = [int.from_bytes(expected[i:i+4], 'little') for i in range(0, len(expected), 4)]
        functions = ('vkCreateDescriptorSetLayout', 'vkCreateDescriptorPool',
                     'vkAllocateDescriptorSets', 'vkUpdateDescriptorSets',
                     'vkCmdBindDescriptorSets', 'vkCmdPipelineBarrier')
        replace_once('VK_FN(vkCreateBuffer);', 'VK_FN(vkCreateBuffer);\n' +
            '\n'.join('VK_FN(' + f + ');' for f in functions))
        replace_once('   LOAD_DEVICE(device, vkCreateBuffer);',
            '   LOAD_DEVICE(device, vkCreateBuffer);\n' + '\n'.join(
                '   LOAD_DEVICE(device, ' + f + ');' for f in functions))
        setup = Path(__file__).with_name('vulkan-probe-storage.inc.c').read_text()
        setup = setup.replace('PROBE_WORD_COUNT', str(len(words)))
        setup = setup.replace('PROBE_STAGE', 'VK_SHADER_STAGE_' + args.storage_stage.upper() + '_BIT')
        replace_once('   const VkPipelineLayoutCreateInfo layout_info = {', setup +
            '\n   const VkPipelineLayoutCreateInfo layout_info = {\n'
            '      .setLayoutCount = 1, .pSetLayouts = &storage_layout,')
        replace_once('   vkCmdDraw(command_buffer, 3, 1, 0, 0);',
            '   vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,\n'
            '      pipeline_layout, 0, 1, &storage_set, 0, NULL);\n'
            '   vkCmdDraw(command_buffer, 3, 1, 0, 0);')
        replace_once('   check(vkEndCommandBuffer(command_buffer), "vkEndCommandBuffer");',
            '   VkMemoryBarrier storage_host = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,\n'
            '      .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT, .dstAccessMask = VK_ACCESS_HOST_READ_BIT};\n'
            '   vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,\n'
            '      VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &storage_host, 0, NULL, 0, NULL);\n'
            '   check(vkEndCommandBuffer(command_buffer), "vkEndCommandBuffer");')
        validation = ('   const uint32_t storage_expected[] = {' + ','.join(str(x)+'u' for x in words) + '};\n'
            '   bool storage_failed = false;\n'
            '   for (unsigned i = 0; i < sizeof(storage_expected)/4 + 16; ++i) {\n'
            '      uint32_t expected = i < sizeof(storage_expected)/4 ? storage_expected[i] : 0xcdcdcdcdu;\n'
            '      if (storage_map[i] != expected) {\n'
            '         fprintf(stderr, "SSBO[%u]=%u expected=%u\\n", i, storage_map[i], expected);\n'
            '         storage_failed = true;\n'
            '      }\n'
            '   }\n'
            '   if (storage_failed) return EXIT_FAILURE;\n'
            '   puts("PASS: exact shader storage output and 64-byte guard");\n')
        replace_once('   printf(PROBE_NAME ": retired %u submissions\\n", repeats);', validation +
            '   printf(PROBE_NAME ": retired %u submissions\\n", repeats);')
    if args.front_face == 'cw':
        replace_once('      .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,',
                     '      .frontFace = VK_FRONT_FACE_CLOCKWISE,')
    if args.indexed:
        for function in ('vkCmdBindIndexBuffer', 'vkCmdDrawIndexed'):
            replace_once('VK_FN(vkCreateBuffer);', 'VK_FN(vkCreateBuffer);\nVK_FN(' + function + ');')
            replace_once('   LOAD_DEVICE(device, vkCreateBuffer);',
                         '   LOAD_DEVICE(device, vkCreateBuffer);\n   LOAD_DEVICE(device, ' + function + ');')
        replace_once('      .size = sizeof(vertices),', '      .size = sizeof(vertices) + 12,')
        replace_once('      .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,',
                     '      .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,')
        replace_once('vkMapMemory(device, vertex_memory, 0, sizeof(vertices), 0,',
                     'vkMapMemory(device, vertex_memory, 0, sizeof(vertices) + 12, 0,')
        replace_once('   memcpy(vertex_map, vertices, sizeof(vertices));',
            '   memcpy(vertex_map, vertices, sizeof(vertices));\n'
            '   const uint32_t indices[] = {0, 1, 2};\n'
            '   memcpy((char *)vertex_map + sizeof(vertices), indices, sizeof(indices));')
        replace_once('   vkCmdDraw(command_buffer, 3, 1, 0, 0);',
            '   vkCmdBindIndexBuffer(command_buffer, vertex_buffer, sizeof(vertices), VK_INDEX_TYPE_UINT32);\n'
            '   vkCmdDrawIndexed(command_buffer, 3, 1, 0, 0, 0);')
    if args.reference_host:
        replace_once('      if (candidate.deviceID != T8140_DEVICE_ID)',
                     '      if (!strstr(candidate.deviceName, "llvmpipe"))')
    if args.format == 'srgb8':
        replace_once('#define IMAGE_FORMAT VK_FORMAT_R8G8B8A8_UNORM',
                     '#define IMAGE_FORMAT VK_FORMAT_R8G8B8A8_SRGB')
    if args.right_rgba:
        left = ', '.join(map(str, args.rgba))
        right = ', '.join(map(str, args.right_rgba))
        replace_once('static const uint8_t expected[4] = {' + left + '};',
            'static const uint8_t expected_left[4] = {' + left + '};\n'
            '   static const uint8_t expected_right[4] = {' + right + '};')
        replace_once('      const uint8_t *actual = &pixels[pixel * sizeof(expected)];',
            '      uint8_t expected[4];\n'
            '      memcpy(expected, pixel % IMAGE_WIDTH < IMAGE_WIDTH / 2\n'
            '                       ? expected_left : expected_right, sizeof(expected));\n'
            '      const uint8_t *actual = &pixels[pixel * sizeof(expected)];')
    if args.rasterizer_discard:
        replace_once('      .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,',
            '      .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,\n'
            '      .rasterizerDiscardEnable = VK_TRUE,')
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
        if args.transform_feedback_resume:
            replace_once('const VkDeviceSize xfb_size = 64, xfb_offset = 0;',
                         'const VkDeviceSize xfb_size = 112, xfb_offset = 0;')
            replace_once('   memset(xfb_map, 0xcd, xfb_size);', '''   memset(xfb_map, 0xcd, xfb_size);
   xfb_info.size = 4;
   xfb_info.usage = VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_COUNTER_BUFFER_BIT_EXT;
   VkBuffer counter_buffer;
   check(vkCreateBuffer(device, &xfb_info, NULL, &counter_buffer), "vkCreateBuffer(counter)");
   vkGetBufferMemoryRequirements(device, counter_buffer, &xfb_req);
   xfb_alloc.allocationSize = xfb_req.size;
   xfb_alloc.memoryTypeIndex = find_memory_type(&memory_properties, xfb_req.memoryTypeBits,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
   VkDeviceMemory counter_memory;
   check(vkAllocateMemory(device, &xfb_alloc, NULL, &counter_memory), "vkAllocateMemory(counter)");
   check(vkBindBufferMemory(device, counter_buffer, counter_memory, 0), "vkBindBufferMemory(counter)");
   uint32_t *counter_map;
   check(vkMapMemory(device, counter_memory, 0, 4, 0, (void **)&counter_map), "vkMapMemory(counter)");
   *counter_map = 0xcdcdcdcd;''')
            replace_once('   vkCmdEndTransformFeedbackEXT(command_buffer, 0, 0, NULL, NULL);',
                '''   vkCmdEndTransformFeedbackEXT(command_buffer, 0, 1, &counter_buffer, &xfb_offset);
   vkCmdEndRenderPass(command_buffer);
   const VkMemoryBarrier counter_resume = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
      .srcAccessMask = VK_ACCESS_TRANSFORM_FEEDBACK_COUNTER_WRITE_BIT_EXT,
      .dstAccessMask = VK_ACCESS_TRANSFORM_FEEDBACK_COUNTER_READ_BIT_EXT,
   };
   vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFORM_FEEDBACK_BIT_EXT,
      VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, 0, 1, &counter_resume, 0, NULL, 0, NULL);
   vkCmdBeginRenderPass(command_buffer, &render_begin, VK_SUBPASS_CONTENTS_INLINE);
   vkCmdBeginTransformFeedbackEXT(command_buffer, 0, 1, &counter_buffer, &xfb_offset);
   vkCmdDraw(command_buffer, 3, 1, 0, 0);
   vkCmdEndTransformFeedbackEXT(command_buffer, 0, 1, &counter_buffer, &xfb_offset);''')
            replace_once('      .srcAccessMask = VK_ACCESS_TRANSFORM_FEEDBACK_WRITE_BIT_EXT,',
                '      .srcAccessMask = VK_ACCESS_TRANSFORM_FEEDBACK_WRITE_BIT_EXT |\n'
                '                       VK_ACCESS_TRANSFORM_FEEDBACK_COUNTER_WRITE_BIT_EXT,')
            replace_once('static const float xfb_expected[3][4]',
                         'static const float xfb_expected[6][4]')
            replace_once('      {-1, -1, 0, 1}, {3, -1, 0, 1}, {-1, 3, 0, 1},',
                '      {-1, -1, 0, 1}, {3, -1, 0, 1}, {-1, 3, 0, 1},\n'
                '      {-1, -1, 0, 1}, {3, -1, 0, 1}, {-1, 3, 0, 1},')
            replace_once('for (unsigned i = 0; i < 3; ++i)',
                         'for (unsigned i = 0; i < 6; ++i)')
            replace_once('   if (!xfb_failed)\n', '''   if (*counter_map != 96) {
      fprintf(stderr, "FAIL: transform feedback counter %u, expected 96\\n", *counter_map);
      xfb_failed = true;
   }
   if (!xfb_failed)
''')
            source = source.replace('three exact vec4 positions and buffer guard',
                                    'six exact vec4 positions, resumed counter and buffer guard')
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
    if args.geometry:
        shutil.copyfile(args.geometry, output / 'probe.geom')
        commands.append(['glslangValidator', '-V', '--target-env', 'vulkan1.1',
                         '--vn', 'probe_geom_spv', '-o', str(output / 'probe_geom.h'),
                         str(output / 'probe.geom')])
    if args.storage_expected:
        shutil.copyfile(args.storage_expected, output / 'storage-expected.bin')
    target_flags = [] if (args.host_compile_only or args.reference_host) else [
         '--target=aarch64-linux-gnu', '--sysroot=' + str(args.sysroot),
         '--gcc-install-dir=' + str(args.sysroot /
             'usr/lib/gcc/aarch64-unknown-linux-gnu/16.1.1'), '-fuse-ld=lld']
    binary = output / ('probe-host-compile-only' if args.host_compile_only
                       else 'probe-reference-host' if args.reference_host else 'probe-arm64')
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
        'format': args.format, 'samples': args.samples, 'rgba': args.rgba, 'right_rgba': args.right_rgba,
        'occlusion_query': args.occlusion_query,
        'transform_feedback': args.transform_feedback,
        'transform_feedback_resume': args.transform_feedback_resume,
        'rasterizer_discard': args.rasterizer_discard,
        'allow_unadvertised_samples': args.allow_unadvertised_samples,
        'host_compile_only': args.host_compile_only,
        'record_command_buffer': args.record_command_buffer,
        'front_face': args.front_face, 'indexed': args.indexed, 'extent': args.extent, 'geometry': bool(args.geometry), 'storage_stage': args.storage_stage,
        'reference_host': args.reference_host,
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
