# Corrections after the frozen 1,000-case baseline

Do not overwrite the baseline outcomes. Retests below use separately recorded
source/library/test identities and preserve each case's comparison.

## Render-pass crashes (baseline indices 0 and 848)

Both now Pass in `20260913T184532088442Z-cts-crashes-input-aspects`.
The historical CTS builds two subpasses: subpass 0 has no input attachments,
subpass 1 reads attachment 0. Its depth/stencil input-aspect records incorrectly
name subpass 0, causing Mesa's inputAttachmentIndex assertion. The existing
color correction was incomplete; the corrected variant fixes all six equivalent
depth/stencil constructions too. Images, draws and comparisons are unchanged.

`input-aspects-combined.patch` includes the earlier identified CTS corrections,
with the input-aspect correction expanded. Exact binary hash and restored
original source/executable checks are in `input-aspects-build.json`.
Remote binary: `/root/cts-1.0.2.6/deqp-vk-input-aspects`.
This fixes invalid CTS setup; no Mesa assertion was removed or bypassed.
