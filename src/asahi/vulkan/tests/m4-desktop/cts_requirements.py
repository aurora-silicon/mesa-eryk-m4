"""Reviewed eligibility rules for the pinned Vulkan 1.0 CTS list.

Unknown requirements are excluded, never guessed from a prior test outcome.
See README-cts.md for coverage boundaries and how to extend this catalog.
"""
import hashlib
import json
from pathlib import Path
import re

CATALOG = Path(__file__).with_name('cts-requirements-vulkan10.json')


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


def make_profile(info_path, identity_path):
    raw = info_path.read_bytes()
    identity = json.loads(identity_path.read_text())
    # Exact ICD/module hashes bind advertised support to the measured driver.
    files = {p: h for p, h in identity['files'].items()
             if p.endswith('/libvulkan_asahi.so') or '/drivers/gpu/drm/asahi/' in p}
    if not any(p.endswith('/libvulkan_asahi.so') for p in files):
        raise ValueError('Identity must contain the measured Asahi Vulkan library hash')
    device = json.loads(raw)['capabilities']['device']
    return dict(schema=1, vulkaninfo_sha256=digest(raw), device=device,
                identity=dict(kernel=identity['kernel'], files=files),
                source=str(info_path.resolve()))


class Classifier:
    def __init__(self, profile_path, mustpass_raw):
        self.catalog_raw = CATALOG.read_bytes()
        self.catalog = json.loads(self.catalog_raw)
        if digest(mustpass_raw) != self.catalog['mustpass_sha256']:
            raise ValueError('Unreviewed mustpass list: update the requirements catalog first')
        self.profile_raw = profile_path.read_bytes()
        self.profile = json.loads(self.profile_raw)
        if self.profile.get('schema') != 1 or not self.profile.get('identity', {}).get('files'):
            raise ValueError('Use cts-sample.py profile to bind capabilities to a driver identity')
        self.device = self.profile['device']
        self.features = self.device['features']['VkPhysicalDeviceFeatures']

    def classify(self, case):
        requirements = []

        def feature(name):
            requirements.append(dict(kind='feature', name=name, mandatory=False,
                                     present=self.features.get(name, False)))

        def extension(name):
            requirements.append(dict(kind='extension', name=name, mandatory=False,
                                     present=name in self.device['extensions']))

        def fmt(name, bit):
            name = 'VK_FORMAT_' + name.upper()
            flag = 'VK_FORMAT_FEATURE_' + bit + '_BIT'
            required = name in self.catalog['required_optimal'][bit]
            present = flag in self.device['formats'].get(name, {}).get(
                'VkFormatProperties', {}).get('optimalTilingFeatures', [])
            requirements.append(dict(kind='optimal_format', name=name, flag=flag,
                                     mandatory=required, present=present))

        def done(rule):
            missing = [r for r in requirements if not r['mandatory'] and not r['present']]
            label = ('not-advertised' if missing else
                     'advertised' if any(not r['mandatory'] for r in requirements) else 'required')
            return dict(classification=label, rule=rule, requirements=requirements)

        if 'win32' in case or '.android.' in case:
            return dict(classification='platform-excluded', rule='Linux target', requirements=[])

        # These tests audit the baseline itself; a false capability bit must
        # not hide a required-support failure. Querying optional formats is core.
        if re.fullmatch(r'dEQP-VK.api.info.(instance|device).\w+', case):
            return done('core-info')
        if case.startswith('dEQP-VK.api.info.image_format_properties.'):
            return done('core-image-format-query')
        if case.startswith('dEQP-VK.api.info.format_properties.'):
            # YCbCr has additional feature/extension gates; not mapped here.
            if (case.rsplit('.', 1)[1] not in ('depth_stencil', 'compressed_formats') and
                    'VK_FORMAT_' + case.rsplit('.', 1)[1].upper() not in self.catalog['core_formats']):
                return dict(classification='unclassified', rule='YCbCr query gates not mapped', requirements=[])
            return done('core-format-query')
        if case.startswith('dEQP-VK.pipeline.depth.format_features.'):
            return done('core-depth-format-alternatives')

        # The reviewed ShaderRender generators use float/int/bool <=32-bit,
        # vertex/fragment stages, RGBA8, one sample, no optional shader stages.
        if re.fullmatch(r'dEQP-VK.glsl.(operator|matrix|loops).+_(vertex|fragment)', case):
            return done('core-shader-render')

        if case.startswith('dEQP-VK.tessellation.invariance.'):
            feature('tessellationShader')
            feature('vertexPipelineStoresAndAtomics')
            if '.tess_coord_component_range.' not in case:
                feature('geometryShader')
            return done('tessellation-invariance')

        m = re.fullmatch(r'dEQP-VK.api.copy_and_blit.(core|dedicated_allocation).'
                         r'blit_image.all_formats.color.([^.]+).([^.]+).'
                         r'(?:optimal|general)_(?:optimal|general)_(linear|nearest)', case)
        if m:
            allocation, src, dst, filtering = m.groups()
            if allocation == 'dedicated_allocation':
                extension('VK_KHR_dedicated_allocation')
                extension('VK_KHR_get_memory_requirements2')
            fmt(src, 'BLIT_SRC')
            fmt(dst, 'BLIT_DST')
            if filtering == 'linear':
                # Vulkan filters the source. Historical CTS also checks the
                # destination incorrectly; that defect is not a requirement.
                fmt(src, 'SAMPLED_IMAGE_FILTER_LINEAR')
            return done('color-blit-2d-optimal')

        # Restrict this mapping to uncompressed 2D/2D-array, whose modest sizes
        # are inside baseline limits. Compressed/3D/cube combinations need
        # separate image-format-property evidence, not merely format flags.
        m = re.fullmatch(r'dEQP-VK\.pipeline\.(image|image_view|sampler)\.(?:.*\.)?'
                         r'view_type.(2d|2d_array).format.([^.]+).(.+)', case)
        if m:
            family, view, name, tail = m.groups()
            if name.endswith('_block'):
                return dict(classification='unclassified', rule='compressed image combination not mapped', requirements=[])
            if family == 'image':
                count = re.match(r'count_(\d+)\.size\.', tail)
                if not count or int(count[1]) > 4:
                    return dict(classification='unclassified', rule='image count/limits not mapped', requirements=[])
                if int(count[1]) > 1:
                    feature('shaderSampledImageArrayDynamicIndexing')
                if '.dedicated_allocation.' in case:
                    extension('VK_KHR_dedicated_allocation')
                    extension('VK_KHR_get_memory_requirements2')
            fmt(name, 'SAMPLED_IMAGE')
            if 'linear' in tail.split('.'):
                fmt(name, 'SAMPLED_IMAGE_FILTER_LINEAR')
            if 'mirror_clamp_to_edge' in tail:
                extension('VK_KHR_sampler_mirror_clamp_to_edge')
            return done('sampled-uncompressed-2d')

        return dict(classification='unclassified', rule='requirements not mapped', requirements=[])
