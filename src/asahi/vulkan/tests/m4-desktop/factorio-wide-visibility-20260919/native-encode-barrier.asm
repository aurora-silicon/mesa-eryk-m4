
build/AGXMetalG16G_B0.arm64e:	file format mach-o arm64

Disassembly of section __TEXT,__text:

00000000002f00f0 <__ZN3AGX14ComputeContextINS_6HAL2008EncodersENS1_7ClassesENS1_10ObjClassesENS1_19CommandEncodingNextENS1_32EncoderComputeServiceClassesNextEE13encodeBarrierE19eAGXDataBufferPools21eAGXBarrierVisibilitybb>:
  2f00f0: d503237f     	pacibsp
  2f00f4: d10103ff     	sub	sp, sp, #0x40
  2f00f8: a90157f6     	stp	x22, x21, [sp, #0x10]
  2f00fc: a9024ff4     	stp	x20, x19, [sp, #0x20]
  2f0100: a9037bfd     	stp	x29, x30, [sp, #0x30]
  2f0104: 9100c3fd     	add	x29, sp, #0x30
  2f0108: aa0103f4     	mov	x20, x1
  2f010c: aa0003f3     	mov	x19, x0
  2f0110: 91400415     	add	x21, x0, #0x1, lsl #12  ; =0x1000
  2f0114: 121f7828     	and	w8, w1, #0xfffffffe
  2f0118: 7100591f     	cmp	w8, #0x16
  2f011c: 54000181     	b.ne	0x2f014c <__ZN3AGX14ComputeContextINS_6HAL2008EncodersENS1_7ClassesENS1_10ObjClassesENS1_19CommandEncodingNextENS1_32EncoderComputeServiceClassesNextEE13encodeBarrierE19eAGXDataBufferPools21eAGXBarrierVisibilitybb+0x5c>
  2f0120: f9444668     	ldr	x8, [x19, #0x888]
  2f0124: b4000148     	cbz	x8, 0x2f014c <__ZN3AGX14ComputeContextINS_6HAL2008EncodersENS1_7ClassesENS1_10ObjClassesENS1_19CommandEncodingNextENS1_32EncoderComputeServiceClassesNextEE13encodeBarrierE19eAGXDataBufferPools21eAGXBarrierVisibilitybb+0x5c>
  2f0128: f949d268     	ldr	x8, [x19, #0x13a0]
  2f012c: f949d663     	ldr	x3, [x19, #0x13a8]
  2f0130: f949da64     	ldr	x4, [x19, #0x13b0]
  2f0134: 9121a260     	add	x0, x19, #0x868
  2f0138: aa1303e1     	mov	x1, x19
  2f013c: aa0203f6     	mov	x22, x2
  2f0140: aa0803e2     	mov	x2, x8
  2f0144: 97ff5366     	bl	0x2c4edc <__ZN3AGX35ComputeIndirectExecutionContextGen4INS_6HAL2008EncodersENS1_7ClassesENS1_10ObjClassesENS1_19CommandEncodingNextENS1_32EncoderComputeServiceClassesNextEE29patchCDMControlStreamAndResetEPNS1_22EncoderComputeServiceBEPv11AddressViewIvE>
  2f0148: aa1603e2     	mov	x2, x22
  2f014c: 3957e2a8     	ldrb	w8, [x21, #0x5f8]
  2f0150: 7100051f     	cmp	w8, #0x1
  2f0154: 54000401     	b.ne	0x2f01d4 <__ZN3AGX14ComputeContextINS_6HAL2008EncodersENS1_7ClassesENS1_10ObjClassesENS1_19CommandEncodingNextENS1_32EncoderComputeServiceClassesNextEE13encodeBarrierE19eAGXDataBufferPools21eAGXBarrierVisibilitybb+0xe4>
  2f0158: f94b0268     	ldr	x8, [x19, #0x1600]
  2f015c: f94b0669     	ldr	x9, [x19, #0x1608]
  2f0160: eb09011f     	cmp	x8, x9
  2f0164: 540008e0     	b.eq	0x2f0280 <__ZN3AGX14ComputeContextINS_6HAL2008EncodersENS1_7ClassesENS1_10ObjClassesENS1_19CommandEncodingNextENS1_32EncoderComputeServiceClassesNextEE13encodeBarrierE19eAGXDataBufferPools21eAGXBarrierVisibilitybb+0x190>
  2f0168: 52800028     	mov	w8, #0x1                ; =1
  2f016c: 381f8128     	sturb	w8, [x9, #-0x8]
  2f0170: 39640a6a     	ldrb	w10, [x19, #0x902]
  2f0174: 381f912a     	sturb	w10, [x9, #-0x7]
  2f0178: f94b1269     	ldr	x9, [x19, #0x1620]
  2f017c: 297bad2a     	ldp	w10, w11, [x9, #-0x24]
  2f0180: 1100054a     	add	w10, w10, #0x1
  2f0184: b81dc12a     	stur	w10, [x9, #-0x24]
  2f0188: b9563669     	ldr	w9, [x19, #0x1634]
  2f018c: 0b0a016a     	add	w10, w11, w10
  2f0190: 6b0a013f     	cmp	w9, w10
  2f0194: 54000762     	b.hs	0x2f0280 <__ZN3AGX14ComputeContextINS_6HAL2008EncodersENS1_7ClassesENS1_10ObjClassesENS1_19CommandEncodingNextENS1_32EncoderComputeServiceClassesNextEE13encodeBarrierE19eAGXDataBufferPools21eAGXBarrierVisibilitybb+0x190>
  2f0198: b9537269     	ldr	w9, [x19, #0x1370]
  2f019c: 7100013f     	cmp	w9, #0x0
  2f01a0: 1a9f07e4     	cset	w4, ne
  2f01a4: b9537665     	ldr	w5, [x19, #0x1374]
  2f01a8: b90007e8     	str	w8, [sp, #0x4]
  2f01ac: 91006261     	add	x1, x19, #0x18
  2f01b0: 910013e6     	add	x6, sp, #0x4
  2f01b4: 52800440     	mov	w0, #0x22               ; =34
  2f01b8: 52800302     	mov	w2, #0x18               ; =24
  2f01bc: 52800003     	mov	w3, #0x0                ; =0
  2f01c0: 97fe25bf     	bl	0x2798bc <__Z24agxaReserveCDMTokenSpaceIN3AGX6HAL2008EncodersENS1_19DataBufferAllocatorEEv19eAGXDataBufferPoolsRT0_mbbjRKNS0_18FlagsConfigurationI26eAGXDataBufferReserveFlagsEE>
  2f01c4: b9563668     	ldr	w8, [x19, #0x1634]
  2f01c8: 11000508     	add	w8, w8, #0x1
  2f01cc: b9163668     	str	w8, [x19, #0x1634]
  2f01d0: 1400002c     	b	0x2f0280 <__ZN3AGX14ComputeContextINS_6HAL2008EncodersENS1_7ClassesENS1_10ObjClassesENS1_19CommandEncodingNextENS1_32EncoderComputeServiceClassesNextEE13encodeBarrierE19eAGXDataBufferPools21eAGXBarrierVisibilitybb+0x190>
  2f01d4: 51005a88     	sub	w8, w20, #0x16
  2f01d8: 71000d1f     	cmp	w8, #0x3
  2f01dc: 54000528     	b.hi	0x2f0280 <__ZN3AGX14ComputeContextINS_6HAL2008EncodersENS1_7ClassesENS1_10ObjClassesENS1_19CommandEncodingNextENS1_32EncoderComputeServiceClassesNextEE13encodeBarrierE19eAGXDataBufferPools21eAGXBarrierVisibilitybb+0x190>
  2f01e0: 39640a68     	ldrb	w8, [x19, #0x902]
  2f01e4: 394d96a3     	ldrb	w3, [x21, #0x365]
  2f01e8: b9537269     	ldr	w9, [x19, #0x1370]
  2f01ec: b9537665     	ldr	w5, [x19, #0x1374]
  2f01f0: 7100013f     	cmp	w9, #0x0
  2f01f4: 1a9f07e4     	cset	w4, ne
  2f01f8: 34000162     	cbz	w2, 0x2f0224 <__ZN3AGX14ComputeContextINS_6HAL2008EncodersENS1_7ClassesENS1_10ObjClassesENS1_19CommandEncodingNextENS1_32EncoderComputeServiceClassesNextEE13encodeBarrierE19eAGXDataBufferPools21eAGXBarrierVisibilitybb+0x134>
  2f01fc: 5282bfc9     	mov	w9, #0x15fe             ; =5630
  2f0200: 72ac0049     	movk	w9, #0x6002, lsl #16
  2f0204: 52803fca     	mov	w10, #0x1fe             ; =510
  2f0208: 72ac004a     	movk	w10, #0x6002, lsl #16
  2f020c: 7100011f     	cmp	w8, #0x0
  2f0210: 1a890155     	csel	w21, w10, w9, eq
  2f0214: b9000fff     	str	wzr, [sp, #0xc]
  2f0218: 91006261     	add	x1, x19, #0x18
  2f021c: 910033e6     	add	x6, sp, #0xc
  2f0220: 1400000a     	b	0x2f0248 <__ZN3AGX14ComputeContextINS_6HAL2008EncodersENS1_7ClassesENS1_10ObjClassesENS1_19CommandEncodingNextENS1_32EncoderComputeServiceClassesNextEE13encodeBarrierE19eAGXDataBufferPools21eAGXBarrierVisibilitybb+0x158>
  2f0224: 5282ac09     	mov	w9, #0x1560             ; =5472
  2f0228: 72ac0009     	movk	w9, #0x6000, lsl #16
  2f022c: 52802c0a     	mov	w10, #0x160             ; =352
  2f0230: 72ac000a     	movk	w10, #0x6000, lsl #16
  2f0234: 7100011f     	cmp	w8, #0x0
  2f0238: 1a890155     	csel	w21, w10, w9, eq
  2f023c: b9000bff     	str	wzr, [sp, #0x8]
  2f0240: 91006261     	add	x1, x19, #0x18
  2f0244: 910023e6     	add	x6, sp, #0x8
  2f0248: aa1403e0     	mov	x0, x20
  2f024c: 52800082     	mov	w2, #0x4                ; =4
  2f0250: 97fe259b     	bl	0x2798bc <__Z24agxaReserveCDMTokenSpaceIN3AGX6HAL2008EncodersENS1_19DataBufferAllocatorEEv19eAGXDataBufferPoolsRT0_mbbjRKNS0_18FlagsConfigurationI26eAGXDataBufferReserveFlagsEE>
  2f0254: 2a1403e8     	mov	w8, w20
  2f0258: 8b081668     	add	x8, x19, x8, lsl #5
  2f025c: f9402509     	ldr	x9, [x8, #0x48]
  2f0260: b8004535     	str	w21, [x9], #0x4
  2f0264: f9002509     	str	x9, [x8, #0x48]
  2f0268: b9545e69     	ldr	w9, [x19, #0x145c]
  2f026c: b9546268     	ldr	w8, [x19, #0x1460]
  2f0270: 6b08013f     	cmp	w9, w8
  2f0274: 54000042     	b.hs	0x2f027c <__ZN3AGX14ComputeContextINS_6HAL2008EncodersENS1_7ClassesENS1_10ObjClassesENS1_19CommandEncodingNextENS1_32EncoderComputeServiceClassesNextEE13encodeBarrierE19eAGXDataBufferPools21eAGXBarrierVisibilitybb+0x18c>
  2f0278: b9145e68     	str	w8, [x19, #0x145c]
  2f027c: b914627f     	str	wzr, [x19, #0x1460]
  2f0280: a9437bfd     	ldp	x29, x30, [sp, #0x30]
  2f0284: a9424ff4     	ldp	x20, x19, [sp, #0x20]
  2f0288: a94157f6     	ldp	x22, x21, [sp, #0x10]
  2f028c: 910103ff     	add	sp, sp, #0x40
  2f0290: d65f0fff     	retab

