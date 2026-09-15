#!/usr/bin/env python3
"""Capture exact G16 Metal kernel state through the minimal DevMem transport."""

from __future__ import annotations

import argparse
import ctypes
import json
import os
import plistlib
import re
import shutil
import struct
import subprocess
import sys
import time
from pathlib import Path

ABI_VERSION = 6
MAX_READ = 4096
# The 2560x1440 replay-grade ocean output is 14,745,600 bytes. Keep the cap
# tight enough to reject accidental broad mappings while retaining that one
# explicitly validated resource in full.
MAX_PAYLOAD_LENGTH = 16 * 1024 * 1024
GET_INFO = 0
READ_KERNEL_VA = 1
READ_PHYSICAL_RAM = 2
READ_MMIO = 3
CONFIGURE_PTOV = 4
RETAIN_REGISTRY_ENTRY = 8
RETAIN_MEMORY_DESCRIPTOR = 9
READ_MEMORY_DESCRIPTOR = 10
KERNEL_VA = 1
PHYSICAL_RAM = 2
PHYSICAL_MMIO = 3

# Exact 25G83 BootKC symbol addresses. DevMem publishes stripped ``bzero`` as
# a live KASLR anchor; their difference resolves ``ml_static_ptovirt`` without
# carrying a boot-specific address across reboots.
MACOS_25G83_UNSLID_BZERO = 0xFFFFFE000B738DA0
MACOS_25G83_UNSLID_ML_STATIC_PTOVIRT = 0xFFFFFE000B912DF0

# Exact J713 reserved physical objects from the matching ADT/kext evidence.
J713_GFX_HANDOFF_BASE = 0x103FFF70000
J713_GFX_HANDOFF_SIZE = 0x4000
J713_GFX_HANDOFF_USED_SIZE = 0x648
J713_GFX_SHARED_BASE = 0x103FFF78000
J713_GPU_REGION_BASE = 0x103FFFB8000
J713_GPU_REGION_SIZE = 0x4000
J713_UAT_CONTEXT_COUNT = 64
J713_UAT_TTBAT_USED_SIZE = J713_UAT_CONTEXT_COUNT * 16
J713_UAT_PHYS_MASK = 0x000003FFFFFFC000
J713_UAT_PAGE_MASK = 0x3FFF
J713_UAT_VA_MASK = (1 << 43) - 1
J713_UAT_L0_SHIFT = 42
J713_UAT_LEVEL_SHIFTS = (36, 25, 14)
J713_UAT_LEVEL_INDEX_MASKS = (0x3F, 0x7FF, 0x7FF)
# Matching 25G83 AGXUnifiedAddressTranslator::allocateDummyGart() constructs
# one zero-filled fallback page and publishes it at the penultimate 16-KiB
# page below the 0x30000000000 user-GART boundary.  Preserve this leaf's
# payload synchronously with the table topology so the Linux reconstruction
# does not have to infer its contents from a post-dispatch physical read.
J713_UAT_DUMMY_GART_VA = 0x000002FFFFFF8000

# Exact matching G16G firmware fault-retention state. Run* consumers publish
# one generation byte per UAT context at +0x08. The named fault-bank scanner
# copies a faulting context's generation to +0x48 and sets its bit at +0.
J713_FW_CONTEXT_GENERATION_STATE_VA = 0xFFFFFC0000194970
J713_FW_CONTEXT_GENERATION_STATE_SIZE = 0x88
# The loader copies all 42 InitData IOMAP records into firmware BSS. The
# heavily referenced globals at +0x1935a0/+0x193758 are not independent
# pointers: they are the firmware-VA fields of slots 17 and 28 respectively.
# Capture and decode the complete live copy so physical aperture, canonical
# VA, sizes, tag, and mode remain associated.
J713_FW_IOMAP_STATE_VA = 0xFFFFFC00001932F0
J713_FW_IOMAP_RECORD_SIZE = 0x28
J713_FW_IOMAP_RECORD_COUNT = 42
J713_FW_IOMAP_STATE_SIZE = (
    J713_FW_IOMAP_RECORD_SIZE * J713_FW_IOMAP_RECORD_COUNT
)
J713_FW_IOMAP_ROLE_SLOTS = {
    "pmgr_irq_state": 0,
    "pmp_ptd_write": 9,
    "pmp_ptd_read": 10,
    "sgx_main": 17,
    "power_control": 22,
    "pio_type1a": 26,
    "cre_restore": 28,
    "gcpms": 29,
    "afrclkgen": 31,
    "gfxclkgen": 41,
}
J713_FW_SCHED_ENGINE_STATE_VA = 0xFFFFFC0000128D60
J713_FW_SCHED_ENGINE_STATE_SIZE = 0x40
J713_FW_SCHED_QUEUE_TABLE_VA = 0xFFFFFC0000129E90
J713_FW_SCHED_QUEUE_ENTRY_SIZE = 0x38
J713_FW_SCHED_QUEUE_ENTRY_COUNT = 128
J713_FW_SCHED_RUNTIME_POINTER_VA = 0xFFFFFC0000192758
J713_FW_SCHED_RUNTIME_ENGINE_WORK_STATE_OFFSET = 0x264
J713_FW_ENGINE_WORK_COMPUTE_COUNTER_OFFSET = 0x804
J713_FW_SCHED_EVENTS_VA = 0xFFFFFC0000192900
J713_FW_DATA_POINTER_VA = 0xFFFFFC0000192D58
J713_FW_HOST_GLOBALS_POINTER_VA = 0xFFFFFC0000199408
J713_FW_POWER_DATA_POINTER_VA = 0xFFFFFC0000199410
J713_FW_DATA_SCHEDULER_INIT_OFFSET = 0x4FD4
J713_FW_DATA_CONSUMED_GPU_CORE_POWER_ASSERT_OFFSET = 0xEAE8
J713_RUNTIME_FW_BRN_TABLE_OFFSET = 0x2C8
J713_FW_BRN_TABLE_ENTRY_SIZE = 0x8
J713_FW_BRN_TABLE_ENTRY_COUNT = 256
J713_FW_BRN_TABLE_DECODED_SIZE = (
    J713_FW_BRN_TABLE_ENTRY_SIZE * J713_FW_BRN_TABLE_ENTRY_COUNT
)
J713_FW_BRN_TABLE_CAPTURE_SIZE = 0x4000
J713_RUNTIME_PB_DESCRIPTOR_GPU_OFFSET = 0x2D0
J713_RUNTIME_PB_DESCRIPTOR_FIRMWARE_OFFSET = 0x2D8
J713_RUNTIME_UMA_DESCRIPTOR_GPU_OFFSET = 0x2E0
J713_RUNTIME_UMA_DESCRIPTOR_FIRMWARE_OFFSET = 0x2E8
J713_PB_HARDWARE_DESCRIPTOR_SIZE = 0x10
J713_PB_HARDWARE_DESCRIPTOR_COUNT = 256
J713_PB_HARDWARE_TABLE_SIZE = (
    J713_PB_HARDWARE_DESCRIPTOR_SIZE * J713_PB_HARDWARE_DESCRIPTOR_COUNT
)
# AGX's firmware mapping is 16-KiB granular.  Retain the entire mapped leaf so
# bytes outside the decoded 0x1000-byte PB descriptor array remain available
# if the inferred 256-entry bound needs revisiting.
J713_PB_HARDWARE_ALLOCATION_CAPTURE_SIZE = 0x4000
J713_USC_PRIVATE_MEMORY_HARDWARE_DESCRIPTOR_SIZE = 0x20
J713_USC_PRIVATE_MEMORY_HARDWARE_DESCRIPTOR_COUNT = 256
J713_USC_PRIVATE_MEMORY_HARDWARE_TABLE_SIZE = (
    J713_USC_PRIVATE_MEMORY_HARDWARE_DESCRIPTOR_SIZE
    * J713_USC_PRIVATE_MEMORY_HARDWARE_DESCRIPTOR_COUNT
)
J713_RUNTIME_POWER_PERFORMANCE_OFFSET = 0x469
J713_PWRPERF_GPU_POWER_STATE_OFFSET = 0x38
J713_PWRPERF_AUXILIARY_POINTER_OFFSET = 0x6318
J713_PWRPERF_AUXILIARY_CAPTURE_SIZE = 0x4000
J713_INITDATA_RUNTIME_SIZE = 0x4B8
J713_INITDATA_HOST_GLOBALS_SIZE = 0xE48
J713_INITDATA_MAIN_SIZE = 0x2658
J713_INITDATA_PWRPERF_SIZE = 0x6578
J713_INITDATA_FW_DATA_SIZE = 0xEAF0
J713_INITDATA_POWER_DATA_SIZE = 0x670

# Read-only SGX registers already sampled by the Linux lab at the hardware
# launch boundary. Capture the same finite windows while commandprobe holds a
# successful shader in flight, so Linux failures can be compared against a
# positive hardware state rather than inferred from firmware bookkeeping.
J713_SGX_MAIN_BASE = 0x300000000
J713_SGX_SECONDARY_BASE = 0x300D00000
J713_FENDER_SCRATCH_OFFSET = 0x60000
J713_GPU_CORE_POWER_ASSERT_COUNTER_OFFSET = 0
J713_SGX_GPC_PERFORMANCE_STATE_OFFSET = 0xE01000
J713_SGX_ENGINE_ISSUE_OFFSET = 0xC040
J713_SGX_ENGINE_ISSUE_SIZE = 0x20
J713_SGX_ENGINE_AUXILIARY_OFFSET = 0xC238
J713_SGX_ENGINE_AUXILIARY_SIZE = 16 * 8
J713_SGX_INTERRUPT_STATUS_OFFSET = 0x10A00
J713_SGX_INTERRUPT_STATUS_SIZE = 0x20
J713_SGX_HARDWARE_QUEUE_STATUS_OFFSET = 0x18038
J713_SGX_HARDWARE_QUEUE_STATUS_SIZE = 8
J713_SGX_MMU_CONFIG_OFFSET = 0xD08000
J713_SGX_MMU_CONFIG_SIZE = 0x34
J713_SGX_ENGINE_REGISTER_RECORD_SIZE = 12
J713_SGX_ENGINE_REGISTER_CAPTURE_MAX_RECORDS = 512
# Exact finite destination set emitted by the controlled 25G83 RunCompute
# producer. Linux reads the same registers after issue. Keep unknown host
# controls named by offset rather than assigning semantics not yet proved.
J713_SGX_COMPUTE_DESTINATION_REGISTERS = (
    ("compute-preemption-buffer", 0x1A510),
    ("compute-cdm-control-stream", 0x1A420),
    ("compute-state-cell-0", 0x1A4D0),
    ("compute-state-cell-1", 0x1A4D8),
    ("compute-state-cell-2", 0x1A4E0),
    ("compute-state-cell-3", 0x1A4E8),
    ("compute-host-control-1a440", 0x1A440),
    ("compute-host-control-1a458", 0x1A458),
    ("compute-host-control-101d8", 0x101D8),
    ("compute-host-control-1a088", 0x1A088),
    ("compute-host-control-1a090", 0x1A090),
    ("compute-host-control-1a058", 0x1A058),
    ("compute-host-control-1a060", 0x1A060),
    ("compute-host-control-1a0b8", 0x1A0B8),
    ("compute-host-control-1a0c0", 0x1A0C0),
    ("compute-host-control-101d0", 0x101D0),
    ("compute-host-control-0d478", 0x0D478),
    ("compute-host-control-1a0e8", 0x1A0E8),
    ("compute-host-control-107a0", 0x107A0),
    ("compute-host-control-0a598", 0x0A598),
    ("compute-host-control-0d410", 0x0D410),
    ("compute-event-trace", 0x1A540),
    ("compute-event-trace-mirror-014a8", 0x014A8),
    ("compute-event-trace-mirror-0a350", 0x0A350),
    ("compute-context-selector-0", 0x10200),
    ("compute-context-selector-1", 0x10428),
    ("compute-engine-core-selector", 0x14028),
    ("compute-engine-context-selector", 0x14070),
    ("compute-context-store-bank-b-base", 0x10098),
    ("compute-context-store-bank-b-core", 0x10090),
    ("compute-context-store-bank-a-base", 0x0A5C0),
    ("compute-context-store-bank-a-core", 0x0A5C8),
    ("compute-context-store-state", 0x10228),
    ("compute-context-store-trailing-state", 0x140A8),
)
J713_SGX_MMIO_REGIONS = (
    (
        "sgx-global-parameter-buffer-base",
        0x1218,
        8,
    ),
    ("sgx-engine-issue", J713_SGX_ENGINE_ISSUE_OFFSET, J713_SGX_ENGINE_ISSUE_SIZE),
    (
        "sgx-engine-auxiliary",
        J713_SGX_ENGINE_AUXILIARY_OFFSET,
        J713_SGX_ENGINE_AUXILIARY_SIZE,
    ),
    (
        "sgx-usc-exec-base-cp-g14x-hypothesis",
        0x10070,
        8,
    ),
    (
        "sgx-usc-helper-program-g14x-hypothesis",
        0x11840,
        8,
    ),
    (
        "sgx-usc-helper-argument-g14x-hypothesis",
        0x11848,
        8,
    ),
    (
        "sgx-usc-helper-config-g14x-hypothesis",
        0x11F80,
        8,
    ),
    (
        "sgx-usc-iogpu-mode-g14x-hypothesis",
        0x12090,
        8,
    ),
    (
        "sgx-global-usc-private-memory-base",
        0x14000,
        8,
    ),
    (
        "sgx-interrupt-status",
        J713_SGX_INTERRUPT_STATUS_OFFSET,
        J713_SGX_INTERRUPT_STATUS_SIZE,
    ),
    (
        "sgx-hardware-queue-status",
        J713_SGX_HARDWARE_QUEUE_STATUS_OFFSET,
        J713_SGX_HARDWARE_QUEUE_STATUS_SIZE,
    ),
    ("sgx-mmu-config", J713_SGX_MMU_CONFIG_OFFSET, J713_SGX_MMU_CONFIG_SIZE),
)

# A launch-timeline record is deliberately fixed-size and self-described by
# the adjacent JSON metadata. Keeping every raw sample, rather than only
# changes, prevents a short pre-slot transition from being lost to decoding
# assumptions made during capture.
J713_LAUNCH_TIMELINE_MMIO_VALID = 1 << 0
J713_GPU_PERFORMANCE_STATE_OFF = 0
J713_LAUNCH_TIMELINE_RECORD_FORMAT = "<QQII32s32s8s64s56s"
J713_LAUNCH_TIMELINE_RECORD_SIZE = struct.calcsize(
    J713_LAUNCH_TIMELINE_RECORD_FORMAT
)

# Exact macOS 25G83 IOGPU layouts, reconstructed from named methods.
UC_IOGPU_DEVICE = 0x120
DEVICE_RESOURCE_NAMESPACE = 0x10
DEVICE_IOGPU = 0x48
DEVICE_TASK = 0x50
DEVICE_GPU_TASK = 0x58
DEVICE_PID = 0x60
DEVICE_COMMAND_QUEUE_NAMESPACE = 0x88
NAMESPACE_HANDLES = 0x10
NAMESPACE_CAPACITY = 0x28
RESOURCE_TYPE_FLAGS = 0x10
RESOURCE_CLIENT_SHARED = 0x18
RESOURCE_MEMORY = 0x28
RESOURCE_IOGPU = 0x50
RESOURCE_SHARED_MACHINE = 0x68
MEMORY_IOGPU = 0x10
MEMORY_MAPPING_MODE = 0x50
MEMORY_MAPPING_COUNT = 0x54
MEMORY_MAPPING_ARRAY = 0x58
MEMORY_INLINE_MAPPINGS = 0x60
SYSMEM_CLIENT_ADDRESS = 0x88
SYSMEM_DESCRIPTOR = 0x90
MEMORY_MAP_MEMORY = 0x18
MEMORY_MAP_GPU_TASK = 0x20
MEMORY_MAP_GPU_VA = 0x28
QUEUE_SCHEDULER_INDEX = 0x424
QUEUE_GPU_TASK = 0x488
QUEUE_DEVICE = 0x490
QUEUE_PID = 0x498
QUEUE_IOGPU = 0x538
QUEUE_SCHEDULER = 0x540
QUEUE_UNIQUE_ID = 0x558
QUEUE_ACTIVE_CL_CHANNEL = 0x840
QUEUE_ACTIVE_CL_WORK_QUEUE = 0x850
QUEUE_PROTECTION_OPTIONS = 0x550
QUEUE_CL_WORK_QUEUES = 0x620
QUEUE_PROTECTED_CL_WORK_QUEUES = 0x658
QUEUE_CL_WORK_QUEUE_CAPACITY = 0x868
QUEUE_CL_WORK_QUEUE_COUNT = 0x870
AGX_CL_CHANNEL_G16_SIZE = 0x208
AGX_CL_WORK_QUEUE_SIZE = 0x240
CL_CHANNEL_WORK_QUEUE = 0x1E8
CL_CHANNEL_COMMAND_QUEUE = 0x200
CL_WORK_QUEUE_CHANNEL = 0x218
IOGPU_WORK_QUEUE_COMPLETED_HEAD = 0x48
IOGPU_WORK_QUEUE_SUBMITTED_TAIL = 0x4C
IOGPU_WORK_QUEUE_ENQUEUED_TAIL = 0x50
IOGPU_WORK_QUEUE_RING_CAPACITY = 0x54
IOGPU_WORK_QUEUE_COMMAND_RING = 0x58
IOGPU_COMMAND_DESCRIPTOR_SIZE = 0x130
IOGPU_COMMAND_DESCRIPTOR_WORK_QUEUE = 0x50
IOGPU_COMMAND_DESCRIPTOR_COMMAND_QUEUE = 0x60
AGX_CL_COMMAND_DESCRIPTOR_SIZE = 0x7B0
AGX_CL_DESCRIPTOR_FIRMWARE_COMMAND_CPU = 0x3E0
AGX_CL_DESCRIPTOR_FIRMWARE_COMMAND_GPU = 0x3E8
AGX_CL_DESCRIPTOR_FIRMWARE_COMPANION_GPU = 0x3F0
AGX_CL_DESCRIPTOR_SKU_ENCODER = 0x138
AGX_SKU_ENCODER_SIZE = 0x80
AGX_SKU_ENCODER_CPU_BUFFER = 0x28
AGX_SKU_ENCODER_BLOCK_STRIDE = 0x50
AGX_SKU_ENCODER_BLOCK_COUNT = 0x58
AGX_SKU_ENCODER_BLOCK_OFFSET = 0x5C
AGX_SKU_ENCODER_BLOCK_INDEX = 0x60
AGX_SKU_ENCODER_WRITE_CURSOR = 0x68
RUN_COMPUTE_CAPTURE_SIZE = 0x8B0
RUN_COMPUTE_REGISTER_LIST_OFFSET = 0x20
RUN_COMPUTE_REGISTERS_HEADER_OFFSET = 0x720
RUN_COMPUTE_SKU_POINTER_OFFSET = 0x760
RUN_COMPUTE_SKU_SIZE_OFFSET = 0x768
RUN_COMPUTE_JOB_PARAMS2_PREEMPT_BUFFER_OFFSET = 0x798
RUN_COMPUTE_JOB_PARAMS2_CONTROL_STREAM_END_OFFSET = 0x7A0
RUN_COMPUTE_JOB_PARAMS2_G16_CONFIG_OFFSET = 0x7C8
RUN_COMPUTE_ENCODER_GLOBAL_TRACE_ID_OFFSET = 0x7E0
RUN_COMPUTE_ENCODER_UNKNOWN_MASK_OFFSET = 0x7E8
RUN_COMPUTE_JOB_META_OFFSET = 0x800
RUN_COMPUTE_JOB_STAMP_ADDRESS_OFFSET = RUN_COMPUTE_JOB_META_OFFSET + 0x00
RUN_COMPUTE_JOB_FIRMWARE_STAMP_ADDRESS_OFFSET = RUN_COMPUTE_JOB_META_OFFSET + 0x08
RUN_COMPUTE_JOB_STAMP_VALUE_OFFSET = RUN_COMPUTE_JOB_META_OFFSET + 0x10
RUN_COMPUTE_JOB_STAMP_SLOT_OFFSET = RUN_COMPUTE_JOB_META_OFFSET + 0x14
RUN_COMPUTE_JOB_EVENT_CONTROL_INDEX_OFFSET = RUN_COMPUTE_JOB_META_OFFSET + 0x18
RUN_COMPUTE_JOB_FLUSH_STAMPS_OFFSET = RUN_COMPUTE_JOB_META_OFFSET + 0x1C
RUN_COMPUTE_JOB_UUID_OFFSET = 0x820
RUN_COMPUTE_JOB_EVENT_SEQUENCE_OFFSET = 0x824
RUN_COMPUTE_TIMESTAMP_START_OFFSET = 0x834
RUN_COMPUTE_TIMESTAMP_END_OFFSET = 0x83C
RUN_COMPUTE_UMA_POINTER_OFFSET = 0x86A
RUN_COMPUTE_UMA_FLUSH_OFFSET = 0x872
RUN_COMPUTE_CONTEXT_STORE_REQUEST_OFFSET = 0x873
RUN_COMPUTE_CONTEXT_STORE_COMPLETE_OFFSET = 0x87B
RUN_COMPUTE_ENGINE_COMMAND_ADDRESS_OFFSET = 0x883
RUN_COMPUTE_CONTEXT_GENERATION_OFFSET = 0x88B
SKU_SLOT_STATE_ARRAY_POINTER_OFFSET = 0x024
SKU_BINDING_AUXILIARY_LOW_UAT_OFFSET = 0x178
SKU_CONTEXT_STORE_ADDRESS_OFFSETS = (0x188, 0x190, 0x198, 0x1A0)
# The low-UAT context-store allocation is exactly eight 16-KiB leaves. Its
# two 0xf400-byte banks are followed by 0x800 bytes of channel state and a
# final 0x1000-byte trailing-state area. The next two leaves are unmapped;
# USC private-memory blocks begin at +0x28000.
J713_COMPUTE_CHANNEL_CONTEXT_STORE_SIZE = 0x20000
RUN_COMPUTE_REGISTER_WORDS = (
    0x1A510,
    0x1A420,
    0x1A4D0,
    0x1A4D8,
    0x1A4E0,
    0x1A4E8,
    0x1A440,
    0x1A458,
    0x101D9,
    0x1A089,
    0x1A091,
    0x1A059,
    0x1A061,
    0x1A0B9,
    0x1A0C1,
    0x101D1,
    0x0D479,
    0x1A0E9,
    0x107A1,
    0x0A599,
    0x0D411,
    0x1A540,
    0x014A9,
    0x0A351,
)
RUN_COMPUTE_OPTIONAL_PREAMBLE_WORD = 0x017E1
RUN_COMPUTE_OPTIONAL_KRCE_WORD = 0x0A0F1


class ReadRequest(ctypes.Structure):
    _fields_ = [
        ("abi_version", ctypes.c_uint32),
        ("space", ctypes.c_uint32),
        ("width", ctypes.c_uint32),
        ("flags", ctypes.c_uint32),
        ("address", ctypes.c_uint64),
        ("length", ctypes.c_uint64),
    ]


class DevMemInfo(ctypes.Structure):
    _fields_ = [
        ("abi_version", ctypes.c_uint32),
        ("page_size", ctypes.c_uint32),
        ("max_read", ctypes.c_uint64),
        ("selftest_kva", ctypes.c_uint64),
        ("selftest_length", ctypes.c_uint64),
        ("selftest_phys", ctypes.c_uint64),
        ("selftest_phys_length", ctypes.c_uint64),
        ("selftest_phys_kva", ctypes.c_uint64),
        ("dram_base", ctypes.c_uint64),
        ("dram_size", ctypes.c_uint64),
        ("kernel_anchor", ctypes.c_uint64),
        ("physical_translator", ctypes.c_uint64),
        ("iosurface_root_global", ctypes.c_uint64),
        ("iosurface_try_retain", ctypes.c_uint64),
    ]


class PtovConfig(ctypes.Structure):
    _fields_ = [
        ("abi_version", ctypes.c_uint32),
        ("flags", ctypes.c_uint32),
        ("function_address", ctypes.c_uint64),
        ("expected_prefix", ctypes.c_uint8 * 32),
    ]


class RegistryRequest(ctypes.Structure):
    _fields_ = [
        ("abi_version", ctypes.c_uint32),
        ("flags", ctypes.c_uint32),
        ("registry_entry_id", ctypes.c_uint64),
    ]


class RegistryResult(ctypes.Structure):
    _fields_ = [
        ("abi_version", ctypes.c_uint32),
        ("flags", ctypes.c_uint32),
        ("registry_entry_id", ctypes.c_uint64),
        ("kernel_address", ctypes.c_uint64),
    ]


class MemoryDescriptorRequest(ctypes.Structure):
    _fields_ = [
        ("abi_version", ctypes.c_uint32),
        ("flags", ctypes.c_uint32),
        ("descriptor_address", ctypes.c_uint64),
    ]


class MemoryDescriptorResult(ctypes.Structure):
    _fields_ = [
        ("abi_version", ctypes.c_uint32),
        ("flags", ctypes.c_uint32),
        ("descriptor_address", ctypes.c_uint64),
        ("mapped_address", ctypes.c_uint64),
        ("mapped_length", ctypes.c_uint64),
    ]


class MemoryDescriptorReadRequest(ctypes.Structure):
    _fields_ = [
        ("abi_version", ctypes.c_uint32),
        ("flags", ctypes.c_uint32),
        ("offset", ctypes.c_uint64),
        ("length", ctypes.c_uint64),
    ]


class DevMem:
    def __init__(self) -> None:
        self.iokit = ctypes.CDLL(
            "/System/Library/Frameworks/IOKit.framework/IOKit"
        )
        self.libsystem = ctypes.CDLL("/usr/lib/libSystem.B.dylib")
        self.iokit.IOServiceMatching.argtypes = [ctypes.c_char_p]
        self.iokit.IOServiceMatching.restype = ctypes.c_void_p
        self.iokit.IOServiceGetMatchingService.argtypes = [
            ctypes.c_uint,
            ctypes.c_void_p,
        ]
        self.iokit.IOServiceGetMatchingService.restype = ctypes.c_uint
        self.iokit.IOServiceOpen.argtypes = [
            ctypes.c_uint,
            ctypes.c_uint,
            ctypes.c_uint32,
            ctypes.POINTER(ctypes.c_uint),
        ]
        self.iokit.IOServiceOpen.restype = ctypes.c_int
        self.iokit.IOServiceClose.argtypes = [ctypes.c_uint]
        self.iokit.IOObjectRelease.argtypes = [ctypes.c_uint]
        self.iokit.IOConnectCallStructMethod.argtypes = [
            ctypes.c_uint,
            ctypes.c_uint32,
            ctypes.c_void_p,
            ctypes.c_size_t,
            ctypes.c_void_p,
            ctypes.POINTER(ctypes.c_size_t),
        ]
        self.iokit.IOConnectCallStructMethod.restype = ctypes.c_int

        matching = self.iokit.IOServiceMatching(b"DevMemService")
        service = self.iokit.IOServiceGetMatchingService(0, matching)
        if not service:
            raise RuntimeError("DevMemService is not published")
        connection = ctypes.c_uint(0)
        task = ctypes.c_uint.in_dll(self.libsystem, "mach_task_self_").value
        kr = self.iokit.IOServiceOpen(service, task, 0, ctypes.byref(connection))
        self.iokit.IOObjectRelease(service)
        if kr != 0:
            raise RuntimeError(f"IOServiceOpen failed: 0x{kr & 0xffffffff:08x}")
        self.connection = connection.value

    def close(self) -> None:
        if self.connection:
            self.iokit.IOServiceClose(self.connection)
            self.connection = 0

    def _call(self, selector: int, request: ctypes.Structure, result: ctypes.Structure) -> None:
        result_size = ctypes.c_size_t(ctypes.sizeof(result))
        kr = self.iokit.IOConnectCallStructMethod(
            self.connection,
            selector,
            ctypes.byref(request),
            ctypes.sizeof(request),
            ctypes.byref(result),
            ctypes.byref(result_size),
        )
        if kr != 0 or result_size.value != ctypes.sizeof(result):
            raise RuntimeError(
                f"selector {selector}: kr=0x{kr & 0xffffffff:08x} "
                f"size={result_size.value}"
            )

    def get_info(self) -> DevMemInfo:
        result = DevMemInfo()
        result_size = ctypes.c_size_t(ctypes.sizeof(result))
        kr = self.iokit.IOConnectCallStructMethod(
            self.connection,
            GET_INFO,
            None,
            0,
            ctypes.byref(result),
            ctypes.byref(result_size),
        )
        if kr != 0 or result_size.value != ctypes.sizeof(result):
            raise RuntimeError(
                f"get info: kr=0x{kr & 0xffffffff:08x} "
                f"size={result_size.value}"
            )
        if (
            result.abi_version != ABI_VERSION
            or result.page_size != 0x4000
            or result.max_read != MAX_READ
        ):
            raise RuntimeError(
                "incompatible DevMem info: "
                f"ABI={result.abi_version} page={result.page_size} "
                f"max_read={result.max_read}"
            )
        return result

    def retain_registry_entry(self, entry_id: int) -> int:
        request = RegistryRequest(ABI_VERSION, 0, entry_id)
        result = RegistryResult()
        self._call(RETAIN_REGISTRY_ENTRY, request, result)
        if result.abi_version != ABI_VERSION or result.registry_entry_id != entry_id:
            raise RuntimeError("registry retain returned inconsistent data")
        return result.kernel_address

    def retain_memory_descriptor(self, address: int) -> MemoryDescriptorResult:
        request = MemoryDescriptorRequest(ABI_VERSION, 0, address)
        result = MemoryDescriptorResult()
        self._call(RETAIN_MEMORY_DESCRIPTOR, request, result)
        if (
            result.abi_version != ABI_VERSION
            or result.descriptor_address != address
            or result.mapped_length == 0
        ):
            raise RuntimeError("memory descriptor retain returned inconsistent data")
        return result

    def read_memory_descriptor(self, offset: int, length: int) -> bytes:
        output = bytearray()
        while len(output) < length:
            chunk = min(MAX_READ, length - len(output))
            request = MemoryDescriptorReadRequest(
                ABI_VERSION, 0, offset + len(output), chunk
            )
            buffer = ctypes.create_string_buffer(chunk)
            output_size = ctypes.c_size_t(chunk)
            kr = self.iokit.IOConnectCallStructMethod(
                self.connection,
                READ_MEMORY_DESCRIPTOR,
                ctypes.byref(request),
                ctypes.sizeof(request),
                buffer,
                ctypes.byref(output_size),
            )
            if kr != 0 or output_size.value != chunk:
                raise RuntimeError(
                    f"descriptor +0x{request.offset:x}+0x{chunk:x}: "
                    f"kr=0x{kr & 0xffffffff:08x} size={output_size.value}"
                )
            output.extend(buffer.raw[:chunk])
        return bytes(output)

    def read(self, address: int, length: int) -> bytes:
        output = bytearray()
        while len(output) < length:
            chunk = min(MAX_READ, length - len(output))
            request = ReadRequest(
                ABI_VERSION, KERNEL_VA, 0, 0, address + len(output), chunk
            )
            buffer = ctypes.create_string_buffer(chunk)
            output_size = ctypes.c_size_t(chunk)
            kr = self.iokit.IOConnectCallStructMethod(
                self.connection,
                READ_KERNEL_VA,
                ctypes.byref(request),
                ctypes.sizeof(request),
                buffer,
                ctypes.byref(output_size),
            )
            if kr != 0 or output_size.value != chunk:
                raise RuntimeError(
                    f"KVA 0x{request.address:x}+0x{chunk:x}: "
                    f"kr=0x{kr & 0xffffffff:08x} size={output_size.value}"
                )
            output.extend(buffer.raw[:chunk])
        return bytes(output)

    def configure_ptov(self, function_address: int) -> None:
        """Configure this connection's exact-build physical translator."""

        prefix = self.read(function_address, 32)
        config = PtovConfig(ABI_VERSION, 0, function_address)
        config.expected_prefix[:] = prefix
        output_size = ctypes.c_size_t(0)
        kr = self.iokit.IOConnectCallStructMethod(
            self.connection,
            CONFIGURE_PTOV,
            ctypes.byref(config),
            ctypes.sizeof(config),
            None,
            ctypes.byref(output_size),
        )
        if kr != 0 or output_size.value != 0:
            raise RuntimeError(
                f"configure ptov 0x{function_address:x}: "
                f"kr=0x{kr & 0xffffffff:08x} size={output_size.value}"
            )

    def read_physical(self, address: int, length: int) -> bytes:
        """Read physical bytes, falling back for non-PAPT reserved pages."""

        output = bytearray()
        while len(output) < length:
            chunk = min(MAX_READ, length - len(output))
            request = ReadRequest(
                ABI_VERSION,
                PHYSICAL_RAM,
                0,
                0,
                address + len(output),
                chunk,
            )
            buffer = ctypes.create_string_buffer(chunk)
            output_size = ctypes.c_size_t(chunk)
            kr = self.iokit.IOConnectCallStructMethod(
                self.connection,
                READ_PHYSICAL_RAM,
                ctypes.byref(request),
                ctypes.sizeof(request),
                buffer,
                ctypes.byref(output_size),
            )
            if kr != 0 or output_size.value != chunk:
                # SPTM's PAPT intentionally excludes some reserved pages,
                # including J713's PPL handoff and TTBAT.  The existing
                # read-only device mapping selector can still map those
                # physical pages, but accepts one naturally aligned scalar
                # per call.
                output.extend(
                    self._read_physical_mapped(
                        address + len(output), length - len(output)
                    )
                )
                break
            output.extend(buffer.raw[:chunk])
        return bytes(output)

    def _read_physical_mapped(self, address: int, length: int) -> bytes:
        output = bytearray()
        while len(output) < length:
            current = address + len(output)
            remaining = length - len(output)
            width = next(
                width
                for width in (8, 4, 2, 1)
                if remaining >= width and current & (width - 1) == 0
            )
            request = ReadRequest(
                ABI_VERSION, PHYSICAL_MMIO, width, 0, current, width
            )
            buffer = ctypes.create_string_buffer(width)
            output_size = ctypes.c_size_t(width)
            kr = self.iokit.IOConnectCallStructMethod(
                self.connection,
                READ_MMIO,
                ctypes.byref(request),
                ctypes.sizeof(request),
                buffer,
                ctypes.byref(output_size),
            )
            if kr != 0 or output_size.value != width:
                raise RuntimeError(
                    f"physical mapping 0x{current:x}+0x{width:x}: "
                    f"kr=0x{kr & 0xffffffff:08x} size={output_size.value}"
                )
            output.extend(buffer.raw[:width])
        return bytes(output)

    def read_mmio(self, address: int, length: int) -> bytes:
        """Read one finite physical MMIO window through naturally sized loads."""

        return self._read_physical_mapped(address, length)


def u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def u64(data: bytes, offset: int) -> int:
    return struct.unpack_from("<Q", data, offset)[0]


def resolve_macos_25g83_ptov(kernel_anchor: int) -> dict:
    """Resolve the exact-build physical translator from live stripped bzero."""

    if kernel_anchor < MACOS_25G83_UNSLID_BZERO:
        raise RuntimeError(
            f"invalid 25G83 kernel anchor 0x{kernel_anchor:016x}"
        )
    slide = kernel_anchor - MACOS_25G83_UNSLID_BZERO
    address = MACOS_25G83_UNSLID_ML_STATIC_PTOVIRT + slide
    if slide & 0x3FFF or address > 0xFFFFFFFFFFFFFFFF:
        raise RuntimeError(
            f"invalid 25G83 KASLR slide 0x{slide:x} from "
            f"anchor 0x{kernel_anchor:016x}"
        )
    return {
        "build": "25G83",
        "kernel_anchor_symbol": "bzero",
        "kernel_anchor": f"0x{kernel_anchor:016x}",
        "kaslr_slide": f"0x{slide:016x}",
        "physical_translator_symbol": "ml_static_ptovirt",
        "physical_translator": f"0x{address:016x}",
    }


def valid_kva(value: int) -> bool:
    return value != 0 and value >> 48 == 0xFFFF and value & 7 == 0


def valid_graph_kva(value: int) -> bool:
    """Accept CPU kernel pointers, excluding canonical AGX GPU addresses."""

    return valid_kva(value) and value >> 40 == 0xFFFFFE


def find_user_client(pid: int) -> dict:
    payload = subprocess.check_output(
        ["/usr/sbin/ioreg", "-a", "-r", "-c", "AGXDeviceUserClient"]
    )
    entries = plistlib.loads(payload)
    pattern = re.compile(rf"^pid {pid},")
    matches = [e for e in entries if pattern.match(e.get("IOUserClientCreator", ""))]
    if len(matches) != 1:
        raise RuntimeError(f"expected one AGXDeviceUserClient for pid {pid}, got {len(matches)}")
    return matches[0]


def save_blob(directory: Path, name: str, address: int, data: bytes, manifest: dict) -> None:
    path = directory / f"{name}.bin"
    path.write_bytes(data)
    manifest["blobs"][name] = {
        "address": f"0x{address:016x}",
        "length": len(data),
        "file": path.name,
    }


def walk_uat_physical(
    devmem: DevMem,
    context_id: int,
    gpu_va: int,
    table_entry_cache: dict[int, int] | None = None,
) -> dict:
    """Resolve one G16 GPU VA through the live physical 64-slot TTBAT."""

    if not 0 <= context_id < J713_UAT_CONTEXT_COUNT:
        raise RuntimeError(f"invalid UAT context {context_id}")
    # G15 and newer use bit 42 as the two-entry L0 selector, followed by a
    # 6-bit L1 and two 11-bit levels.  Canonical 0xfffffc... firmware aliases
    # must therefore be reduced to 43 bits, not the older 39-bit VA mask.
    walk_va = gpu_va & J713_UAT_VA_MASK
    selector = (walk_va >> J713_UAT_L0_SHIFT) & 1
    ttbat_offset = context_id * 16 + selector * 8
    def read_entry(address: int) -> int:
        if table_entry_cache is not None and address in table_entry_cache:
            return table_entry_cache[address]
        value = u64(devmem.read_physical(address, 8), 0)
        if table_entry_cache is not None:
            table_entry_cache[address] = value
        return value

    ttbr = read_entry(J713_GPU_REGION_BASE + ttbat_offset)
    table = ttbr & J713_UAT_PHYS_MASK
    if table == 0 or not (ttbr & 1):
        raise RuntimeError(
            f"context {context_id} TTBR{selector} is not valid: 0x{ttbr:016x}"
        )
    levels = []
    for level, (shift, index_mask) in enumerate(
        zip(J713_UAT_LEVEL_SHIFTS, J713_UAT_LEVEL_INDEX_MASKS), start=1
    ):
        index = (walk_va >> shift) & index_mask
        entry_address = table + index * 8
        entry = read_entry(entry_address)
        levels.append(
            {
                "level": level,
                "shift": shift,
                "index": index,
                "entry_address": f"0x{entry_address:016x}",
                "entry": f"0x{entry:016x}",
            }
        )
        if not (entry & 1):
            raise RuntimeError(
                f"invalid UAT L{level} entry for 0x{gpu_va:016x}: "
                f"0x{entry:016x}"
            )
        table = entry & J713_UAT_PHYS_MASK
    return {
        "context_id": context_id,
        "gpu_va": f"0x{gpu_va:016x}",
        "address_space": f"TTBR{selector}",
        "ttbr": f"0x{ttbr:016x}",
        "levels": levels,
        "physical_address": f"0x{table | (walk_va & J713_UAT_PAGE_MASK):016x}",
    }


def capture_ordered_context_uat_range(
    devmem: DevMem,
    directory: Path,
    manifest: dict,
    context_id: int,
    name: str,
    gpu_va: int,
    length: int,
    structure: str,
    table_entry_cache: dict[int, int] | None = None,
) -> dict:
    """Save an exact logical byte range while retaining every UAT leaf.

    AGX allocations need not be physically contiguous.  The output remains in
    GPU-VA order, while ``pages`` records the independently resolved physical
    source of each contributing 16-KiB leaf.
    """

    if gpu_va == 0 or not 0 < length <= 64 * 1024 * 1024:
        raise RuntimeError(
            f"invalid bounded context-UAT capture: VA=0x{gpu_va:x} "
            f"length=0x{length:x}"
        )
    capture = {
        "structure": structure,
        "context_id": context_id,
        "gpu_va": f"0x{gpu_va:016x}",
        "length": length,
        "pages": [],
    }
    output_path = directory / f"{name}.bin"
    cursor = gpu_va
    remaining = length
    total_nonzero = 0
    with output_path.open("wb") as output_file:
        while remaining:
            page_gpu_va = cursor & ~J713_UAT_PAGE_MASK
            page_offset = cursor & J713_UAT_PAGE_MASK
            chunk_length = min(remaining, 0x4000 - page_offset)
            walk = walk_uat_physical(
                devmem,
                context_id,
                page_gpu_va,
                table_entry_cache=table_entry_cache,
            )
            physical = int(walk["physical_address"], 16)
            if physical & J713_UAT_PAGE_MASK:
                raise RuntimeError(
                    f"context-UAT leaf is not page-aligned: "
                    f"VA=0x{page_gpu_va:x} PA=0x{physical:x}"
                )
            page = devmem.read_physical(physical, 0x4000)
            chunk = page[page_offset : page_offset + chunk_length]
            file_offset = output_file.tell()
            output_file.write(chunk)
            nonzero = sum(byte != 0 for byte in chunk)
            total_nonzero += nonzero
            capture["pages"].append(
                {
                    "gpu_va": f"0x{page_gpu_va:016x}",
                    "physical_address": f"0x{physical:016x}",
                    "uat_leaf": walk["levels"][-1]["entry"],
                    "page_offset": page_offset,
                    "file_offset": file_offset,
                    "length": chunk_length,
                    "nonzero_bytes": nonzero,
                }
            )
            cursor += chunk_length
            remaining -= chunk_length
    manifest["blobs"][name] = {
        "address_space": "ordered-context-uat-range",
        "context_id": context_id,
        "gpu_va": f"0x{gpu_va:016x}",
        "length": length,
        "file": output_path.name,
    }
    capture.update(
        {
            "file": output_path.name,
            "leaf_count": len(capture["pages"]),
            "nonzero_bytes": total_nonzero,
        }
    )
    return capture


def walk_firmware_uat_physical(devmem: DevMem, gpu_va: int) -> dict:
    """Resolve one canonical firmware VA through the global shared UAT root."""

    # Firmware aliases are canonical sign extensions of a 39-bit VA.  Their
    # independent global root therefore has a 3-bit L1, unlike the 6-bit user
    # roots selected through the 64-slot TTBAT.
    walk_va = gpu_va & ((1 << 39) - 1)
    table = J713_GFX_SHARED_BASE
    levels = []
    for level, (shift, index_mask) in enumerate(
        zip(J713_UAT_LEVEL_SHIFTS, (0x7, 0x7FF, 0x7FF)), start=1
    ):
        index = (walk_va >> shift) & index_mask
        entry_address = table + index * 8
        entry = u64(devmem.read_physical(entry_address, 8), 0)
        levels.append(
            {
                "level": level,
                "shift": shift,
                "index": index,
                "entry_address": f"0x{entry_address:016x}",
                "entry": f"0x{entry:016x}",
            }
        )
        required_bits = 3 if level < 3 else 1
        if (entry & required_bits) != required_bits:
            raise RuntimeError(
                f"invalid firmware UAT L{level} entry for 0x{gpu_va:016x}: "
                f"0x{entry:016x}"
            )
        table = entry & J713_UAT_PHYS_MASK
    return {
        "gpu_va": f"0x{gpu_va:016x}",
        "address_space": "firmware-global",
        "root": f"0x{J713_GFX_SHARED_BASE:016x}",
        "levels": levels,
        "physical_address": f"0x{table | (walk_va & J713_UAT_PAGE_MASK):016x}",
    }


def save_uat_walk_tables(
    devmem: DevMem, directory: Path, manifest: dict, walk: dict
) -> list[str]:
    """Preserve every page-table page consumed by one successful UAT walk."""

    root = walk["root"] if "root" in walk else walk["ttbr"]
    table_addresses = [int(root, 16) & J713_UAT_PHYS_MASK]
    table_addresses.extend(
        int(level["entry"], 16) & J713_UAT_PHYS_MASK
        for level in walk["levels"][:-1]
    )
    files = []
    for address in table_addresses:
        name = f"uat-table-{address:016x}"
        if name not in manifest["blobs"]:
            save_blob(
                directory,
                name,
                address,
                devmem.read_physical(address, 0x4000),
                manifest,
            )
        files.append(f"{name}.bin")
    return files


def snapshot_compute_referenced_pages(
    devmem: DevMem,
    directory: Path,
    manifest: dict,
    command: dict,
    firmware: bytes,
    sku: bytes,
    context_id: int,
    capture_usc_private_memory: bool = False,
) -> None:
    """Preserve pages reached by the selected G16 compute command streams.

    The RunCompute record and SKU stream contain a small set of pointers whose
    pointees are consumed by firmware or the hardware engine but are not IOKit
    resource mappings. Preserve the complete 16-KiB leaf pages here so Linux
    can reproduce their exact bytes instead of reconstructing them from field
    guesses. Multiple roles may intentionally resolve to the same leaf.
    """

    references = [
        (
            "sku-binding-auxiliary-state",
            u64(sku, SKU_BINDING_AUXILIARY_LOW_UAT_OFFSET),
            "context-uat",
            f"sku+0x{SKU_BINDING_AUXILIARY_LOW_UAT_OFFSET:x}",
        ),
        *[
            (
                f"sku-context-store-position-{position}",
                u64(sku, offset),
                "context-uat",
                f"sku+0x{offset:x}",
            )
            for position, offset in enumerate(
                SKU_CONTEXT_STORE_ADDRESS_OFFSETS
            )
        ],
        (
            "sku-slot-state-array",
            u64(sku, SKU_SLOT_STATE_ARRAY_POINTER_OFFSET),
            "firmware-global",
            f"sku+0x{SKU_SLOT_STATE_ARRAY_POINTER_OFFSET:x}",
        ),
        (
            "runcompute-binding-uma-descriptor",
            u64(firmware, RUN_COMPUTE_UMA_POINTER_OFFSET),
            "firmware-global",
            f"runcompute+0x{RUN_COMPUTE_UMA_POINTER_OFFSET:x}",
        ),
        (
            "runcompute-engine-command-resource",
            u64(firmware, RUN_COMPUTE_ENGINE_COMMAND_ADDRESS_OFFSET),
            "firmware-global",
            f"runcompute+0x{RUN_COMPUTE_ENGINE_COMMAND_ADDRESS_OFFSET:x}",
        ),
    ]
    records = command.setdefault("referenced_compute_pages", [])
    existing_roles = {record["role"] for record in records}

    def capture_reference(
        role: str, gpu_va: int, address_space: str, source: str
    ) -> tuple[dict | None, bytes | None]:
        if role in existing_roles or gpu_va == 0:
            return None, None
        record = {
            "role": role,
            "pointer_source": source,
            "gpu_va": f"0x{gpu_va:016x}",
            "address_space": address_space,
        }
        try:
            if address_space == "context-uat":
                walk = walk_uat_physical(devmem, context_id, gpu_va)
            else:
                walk = walk_firmware_uat_physical(devmem, gpu_va)
            physical = int(walk["physical_address"], 16)
            page_address = physical & ~J713_UAT_PAGE_MASK
            page_name = f"uat-payload-{page_address:016x}"
            if page_name not in manifest["blobs"]:
                page = devmem.read_physical(page_address, 0x4000)
                save_blob(directory, page_name, page_address, page, manifest)
            else:
                page = (directory / f"{page_name}.bin").read_bytes()
            record.update(
                {
                    "physical_address": f"0x{physical:016x}",
                    "page_address": f"0x{page_address:016x}",
                    "page_offset": physical & J713_UAT_PAGE_MASK,
                    "page_file": f"{page_name}.bin",
                    "page_nonzero_bytes": sum(byte != 0 for byte in page),
                    "uat_walk": walk,
                    "uat_table_files": save_uat_walk_tables(
                        devmem, directory, manifest, walk
                    ),
                }
            )
        except RuntimeError as error:
            # The live shader gate must always remain releasable. A failed
            # corroborating walk is retained as evidence instead of aborting
            # the collector while the command is held in flight.
            record["error"] = str(error)
        records.append(record)
        existing_roles.add(role)
        return record, page if "page_file" in record else None

    captured = {}
    for role, gpu_va, address_space, source in references:
        record, page = capture_reference(role, gpu_va, address_space, source)
        if record is not None:
            captured[role] = (record, page)

    # These captures walk thousands of adjacent leaves. Cache immutable TTBAT
    # and page-table entries for this held submission so the transport does not
    # reread the same upper-level entries for every 16-KiB payload page.
    ordered_walk_cache: dict[int, int] = {}

    if capture_usc_private_memory:
        context_store_gpu = u64(sku, SKU_CONTEXT_STORE_ADDRESS_OFFSETS[0])
        context_store_capture = {
            "structure": "g16_compute_channel_context_store",
            "context_id": context_id,
            "gpu_va": f"0x{context_store_gpu:016x}",
            "length": J713_COMPUTE_CHANNEL_CONTEXT_STORE_SIZE,
        }
        command["compute_context_store_capture"] = context_store_capture
        try:
            command["compute_context_store_capture"] = (
                capture_ordered_context_uat_range(
                    devmem,
                    directory,
                    manifest,
                    context_id,
                    "compute-context-store",
                    context_store_gpu,
                    J713_COMPUTE_CHANNEL_CONTEXT_STORE_SIZE,
                    "g16_compute_channel_context_store",
                    ordered_walk_cache,
                )
            )
        except RuntimeError as error:
            context_store_capture["error"] = str(error)

    # SKU +0x24 is not another USC private-memory descriptor. Firmware's SKU
    # dispatcher indexes this arena by submission slot and writes 0x30-byte
    # compute records (timestamp handling uses a 0x20-byte stride). Preserve
    # its presently relational canonical members without inventing narrower
    # semantics; several point back into the command and SKU while others
    # reach firmware resources not represented by IOKit mappings.
    slot_state_record, slot_state_page = captured.get(
        "sku-slot-state-array", (None, None)
    )
    if slot_state_record is not None and slot_state_page is not None:
        object_offset = slot_state_record["page_offset"]
        # The producer-owned tail moves as the SKU block is reused, so scan
        # bytewise for the exact canonical firmware-VA prefix instead of
        # freezing offsets observed in one submission.  Every retained role
        # remains positional and every candidate still has to pass a complete
        # firmware UAT walk before any physical byte is read.
        for member_offset in range(0, len(slot_state_page) - object_offset - 7):
            value_offset = object_offset + member_offset
            pointer = u64(slot_state_page, value_offset)
            if pointer >> 40 != 0xFFFFFC:
                continue
            capture_reference(
                f"sku-slot-state-array-member-{member_offset:03x}",
                pointer,
                "firmware-global",
                f"sku-slot-state-array+0x{member_offset:x}",
            )

    # The descriptor copied to packed RunCompute +0x86a is the actual
    # 0x98-byte USC private-memory descriptor.  Follow its pool state, page
    # list, and canonical counter so the raw corpus contains the complete
    # first-level producer contract rather than only the descriptor itself.
    uma_record, uma_page = captured.get(
        "runcompute-binding-uma-descriptor", (None, None)
    )
    if uma_record is not None and uma_page is not None:
        descriptor_offset = uma_record["page_offset"]
        nested = (
            ("runcompute-binding-uma-pool-state", 0x14, "context-uat"),
            ("runcompute-binding-uma-page-list", 0x30, "context-uat"),
            ("runcompute-binding-uma-counter", 0x4C, "firmware-global"),
        )
        for role, member_offset, address_space in nested:
            value_offset = descriptor_offset + member_offset
            if value_offset + 8 > len(uma_page):
                continue
            pointer = u64(uma_page, value_offset)
            if address_space == "context-uat":
                plausible = pointer < (1 << 40)
            else:
                plausible = pointer >> 40 == 0xFFFFFC
            if not pointer or not plausible:
                continue
            nested_record, nested_page = capture_reference(
                role,
                pointer,
                address_space,
                f"runcompute-binding-uma+0x{member_offset:x}",
            )
            if nested_record is not None:
                captured[role] = (nested_record, nested_page)

        if capture_usc_private_memory:
            payload_capture = {
                "structure": "g16_usc_private_memory_leaf_payloads",
                "context_id": context_id,
                "descriptor_gpu_va": uma_record["gpu_va"],
                "pages": [],
            }
            command["usc_private_memory_payload_capture"] = payload_capture
            try:
                total_4k_pages = u32(
                    uma_page, descriptor_offset + 0x2C
                )
                used_page_list_qwords = u32(
                    uma_page, descriptor_offset + 0x48
                )
                if total_4k_pages == 0 or total_4k_pages % 4:
                    raise RuntimeError(
                        "USC private-memory total page count is not a "
                        "nonzero multiple of four"
                    )
                if (
                    used_page_list_qwords == 0
                    or used_page_list_qwords % 8
                ):
                    raise RuntimeError(
                        "USC private-memory page-list use is not a "
                        "nonzero multiple of one 0x40-byte block record"
                    )
                block_count = used_page_list_qwords // 8
                leaf_count = total_4k_pages // 4
                if block_count > 64 or leaf_count > 4096:
                    raise RuntimeError(
                        "USC private-memory payload exceeds bounded capture "
                        f"limits: blocks={block_count} leaves={leaf_count}"
                    )
                if leaf_count % block_count:
                    raise RuntimeError(
                        "USC private-memory leaves do not divide evenly "
                        "across page-list blocks"
                    )
                pool_state_record, _ = captured.get(
                    "runcompute-binding-uma-pool-state", (None, None)
                )
                if pool_state_record is None:
                    raise RuntimeError(
                        "USC private-memory pool-state page was not captured"
                    )
                pool_state_gpu = int(pool_state_record["gpu_va"], 16)
                command["usc_private_memory_pool_state_used_capture"] = (
                    capture_ordered_context_uat_range(
                        devmem,
                        directory,
                        manifest,
                        context_id,
                        "usc-private-memory-pool-state-used",
                        pool_state_gpu,
                        total_4k_pages * 8,
                        "g16_usc_private_memory_pool_state_used_qwords",
                        ordered_walk_cache,
                    )
                )
                page_list_record, page_list_page = captured.get(
                    "runcompute-binding-uma-page-list", (None, None)
                )
                if page_list_record is None or page_list_page is None:
                    raise RuntimeError(
                        "USC private-memory page-list page was not captured"
                    )
                page_list_gpu = int(page_list_record["gpu_va"], 16)
                command["usc_private_memory_page_list_used_capture"] = (
                    capture_ordered_context_uat_range(
                        devmem,
                        directory,
                        manifest,
                        context_id,
                        "usc-private-memory-page-list-used",
                        page_list_gpu,
                        used_page_list_qwords * 8,
                        "g16_usc_private_memory_page_list_used_qwords",
                        ordered_walk_cache,
                    )
                )
                page_list_offset = page_list_record["page_offset"]
                if page_list_offset + block_count * 0x40 > len(page_list_page):
                    raise RuntimeError(
                        "USC private-memory page-list records cross the "
                        "captured leaf page"
                    )
                leaves_per_block = leaf_count // block_count
                output_name = "usc-private-memory-leaves"
                output_path = directory / f"{output_name}.bin"
                nonzero_pages = 0
                with output_path.open("wb") as output_file:
                    for block_index in range(block_count):
                        raw_block = u64(
                            page_list_page,
                            page_list_offset + block_index * 0x40,
                        )
                        if not raw_block & (1 << 61):
                            raise RuntimeError(
                                "USC private-memory page-list block "
                                f"{block_index} lacks BIT(61)"
                            )
                        block_gpu_va = raw_block & ~(1 << 61)
                        for leaf_index in range(leaves_per_block):
                            gpu_va = block_gpu_va + leaf_index * 0x4000
                            walk = walk_uat_physical(
                                devmem,
                                context_id,
                                gpu_va,
                                table_entry_cache=ordered_walk_cache,
                            )
                            physical = int(walk["physical_address"], 16)
                            if physical & J713_UAT_PAGE_MASK:
                                raise RuntimeError(
                                    "USC private-memory leaf did not resolve "
                                    f"page-aligned: VA=0x{gpu_va:x} "
                                    f"PA=0x{physical:x}"
                                )
                            page = devmem.read_physical(physical, 0x4000)
                            file_offset = output_file.tell()
                            output_file.write(page)
                            nonzero = sum(byte != 0 for byte in page)
                            nonzero_pages += bool(nonzero)
                            payload_capture["pages"].append(
                                {
                                    "block_index": block_index,
                                    "leaf_index": leaf_index,
                                    "gpu_va": f"0x{gpu_va:016x}",
                                    "physical_address": f"0x{physical:016x}",
                                    "uat_leaf": walk["levels"][-1]["entry"],
                                    "file_offset": file_offset,
                                    "nonzero_bytes": nonzero,
                                }
                            )
                manifest["blobs"][output_name] = {
                    "address_space": "ordered-context-uat-leaves",
                    "context_id": context_id,
                    "length": output_path.stat().st_size,
                    "file": output_path.name,
                }
                payload_capture.update(
                    {
                        "file": output_path.name,
                        "block_count": block_count,
                        "leaves_per_block": leaves_per_block,
                        "leaf_count": leaf_count,
                        "total_4k_pages": total_4k_pages,
                        "nonzero_leaf_count": nonzero_pages,
                    }
                )
            except RuntimeError as error:
                payload_capture["error"] = str(error)


def snapshot_firmware_context_generation_state(
    devmem: DevMem, directory: Path, manifest: dict
) -> dict:
    """Preserve and decode G16G's exact fault-generation latch structure."""

    walk = walk_firmware_uat_physical(
        devmem, J713_FW_CONTEXT_GENERATION_STATE_VA
    )
    physical = int(walk["physical_address"], 16)
    page_remaining = 0x4000 - (physical & J713_UAT_PAGE_MASK)
    if J713_FW_CONTEXT_GENERATION_STATE_SIZE > page_remaining:
        raise RuntimeError("firmware context-generation state crosses a UAT page")
    payload = devmem.read_physical(
        physical, J713_FW_CONTEXT_GENERATION_STATE_SIZE
    )
    name = "firmware-context-generation-state"
    save_blob(directory, name, physical, payload, manifest)
    return {
        "structure": "g16_fw_fault_generation_state",
        "gpu_va": f"0x{J713_FW_CONTEXT_GENERATION_STATE_VA:016x}",
        "file": f"{name}.bin",
        "retained_fault_context_mask": f"0x{u64(payload, 0):016x}",
        "submitted_generation": list(payload[0x08:0x48]),
        "retained_fault_generation": list(payload[0x48:0x88]),
        "uat_walk": walk,
        "uat_table_files": save_uat_walk_tables(
            devmem, directory, manifest, walk
        ),
    }


def decode_firmware_trace(before: bytes, records: bytes, after: bytes) -> dict:
    """Decode retained 25G83 ktrace slots without claiming an atomic snapshot."""
    if len(before) != 0x30 or len(after) != 0x30 or len(records) != 512 * 0x48:
        raise ValueError("invalid 25G83 trace-ring extent")
    indices = {name: {"consumer": u32(blob, 0), "producer": u32(blob, 0x20)}
               for name, blob in (("before", before), ("after", after))}
    if any(index >= 512 for pair in indices.values() for index in pair.values()):
        raise ValueError("25G83 trace-ring index outside capacity")
    events, other = [], []
    for slot in range(512):
        off = slot * 0x48
        raw = records[off:off + 0x48]
        if not any(raw):
            continue
        kind = u32(raw, 0)
        if kind != 5:
            other.append({"ring_slot": slot, "type": kind, "raw": raw.hex()})
            continue
        tag = u32(raw, 0x2c)
        events.append({"ring_slot": slot, "firmware_tick": u64(raw, 4),
            "thread": tag >> 24, "event": f"0x{tag & 0xffffff:x}",
            "phase": u32(raw, 0x30),
            "args": [f"0x{u64(raw, 12 + i * 8):x}" for i in range(4)]})
    return {"indices": indices, "indices_changed": before != after,
            "events": events, "other_records": other,
            "note": "Sequential read of retained slots, including consumed/stale records and other clients. "
                    "Unchanged indices do not exclude a wrap or torn records. No clock conversion, "
                    "event acknowledgement, or active-command attribution is inferred."}


def snapshot_firmware_scheduler_launch_state(
    devmem: DevMem,
    directory: Path,
    manifest: dict,
    capture_mmio: bool = False,
) -> dict:
    """Capture named G16 scheduler state; execution identity is checked separately."""

    def read_firmware_blob(name: str, gpu_va: int, size: int) -> tuple[bytes, dict]:
        walk = walk_firmware_uat_physical(devmem, gpu_va)
        physical = int(walk["physical_address"], 16)
        page_remaining = 0x4000 - (physical & J713_UAT_PAGE_MASK)
        if size > page_remaining:
            raise RuntimeError(f"{name} crosses a firmware UAT page")
        payload = devmem.read_physical(physical, size)
        save_blob(directory, name, physical, payload, manifest)
        return payload, {
            "gpu_va": f"0x{gpu_va:016x}",
            "file": f"{name}.bin",
            "uat_walk": walk,
            "uat_table_files": save_uat_walk_tables(
                devmem, directory, manifest, walk
            ),
        }

    def read_firmware_range(
        name: str, gpu_va: int, size: int
    ) -> tuple[bytes, dict]:
        """Read a possibly fragmented firmware-UAT allocation page by page."""

        payload = bytearray()
        walks = []
        table_files = []
        cursor = gpu_va
        remaining = size
        while remaining:
            walk = walk_firmware_uat_physical(devmem, cursor)
            physical = int(walk["physical_address"], 16)
            chunk_size = min(
                remaining,
                0x4000 - (physical & J713_UAT_PAGE_MASK),
            )
            payload.extend(devmem.read_physical(physical, chunk_size))
            walks.append(walk)
            for table_file in save_uat_walk_tables(
                devmem, directory, manifest, walk
            ):
                if table_file not in table_files:
                    table_files.append(table_file)
            cursor += chunk_size
            remaining -= chunk_size
        save_blob(directory, name, gpu_va, bytes(payload), manifest)
        manifest["blobs"][name]["address_space"] = "firmware-global"
        return bytes(payload), {
            "gpu_va": f"0x{gpu_va:016x}",
            "size": size,
            "file": f"{name}.bin",
            "uat_walks": walks,
            "uat_table_files": table_files,
        }

    engine, engine_record = read_firmware_blob(
        "firmware-scheduler-engine-state",
        J713_FW_SCHED_ENGINE_STATE_VA,
        J713_FW_SCHED_ENGINE_STATE_SIZE,
    )
    words = [u64(engine, offset) for offset in range(0, len(engine), 8)]
    engine_record.update(
        {
            "structure": "g16_fw_scheduler_engine_state",
            "free_engine_slot_mask": f"0x{words[0]:016x}",
            "engine_limits": [f"0x{words[1]:016x}", f"0x{words[4]:016x}"],
            "active_engine_masks": [
                f"0x{words[2]:016x}",
                f"0x{words[6]:016x}",
            ],
            "unknown_words": {
                "+0x18": f"0x{words[3]:016x}",
                "+0x28": f"0x{words[5]:016x}",
                "+0x38": f"0x{words[7]:016x}",
            },
        }
    )

    iomap, iomap_record = read_firmware_range(
        "firmware-iomap-state",
        J713_FW_IOMAP_STATE_VA,
        J713_FW_IOMAP_STATE_SIZE,
    )
    iomap_records = []
    for slot in range(J713_FW_IOMAP_RECORD_COUNT):
        offset = slot * J713_FW_IOMAP_RECORD_SIZE
        record = {
            "slot": slot,
            "offset": f"0x{offset:03x}",
            "physical": f"0x{u64(iomap, offset):016x}",
            "firmware_va": f"0x{u64(iomap, offset + 0x08):016x}",
            "mapped_size": u32(iomap, offset + 0x10),
            "requested_size": u32(iomap, offset + 0x14),
            "tag": f"0x{u64(iomap, offset + 0x18):016x}",
            "mode": f"0x{u64(iomap, offset + 0x20):016x}",
        }
        if record["firmware_va"] != "0x0000000000000000":
            try:
                firmware_va = int(record["firmware_va"], 16)
                walk = walk_firmware_uat_physical(
                    devmem, firmware_va
                )
                record["firmware_uat_walk"] = walk
                table_files = save_uat_walk_tables(
                    devmem, directory, manifest, walk
                )
                first_page = firmware_va & ~J713_UAT_PAGE_MASK
                end_page = (
                    firmware_va
                    + record["mapped_size"]
                    + J713_UAT_PAGE_MASK
                ) & ~J713_UAT_PAGE_MASK
                page_walks = []
                for page_va in range(first_page, end_page, 0x4000):
                    page_walk = walk_firmware_uat_physical(devmem, page_va)
                    page_walks.append(page_walk)
                    for table_file in save_uat_walk_tables(
                        devmem, directory, manifest, page_walk
                    ):
                        if table_file not in table_files:
                            table_files.append(table_file)
                record["firmware_uat_pages"] = page_walks
                record["firmware_uat_table_files"] = table_files
            except RuntimeError as error:
                # The record array remains independently useful if a capture
                # lands before the firmware-global root publishes the alias.
                record["firmware_uat_error"] = str(error)
        iomap_records.append(record)
    iomap_record.update(
        {
            "structure": "g16_fw_loader_iomap_state",
            "records": iomap_records,
            "role_slots": J713_FW_IOMAP_ROLE_SLOTS,
            "role_bases": {
                role: iomap_records[slot]["firmware_va"]
                for role, slot in J713_FW_IOMAP_ROLE_SLOTS.items()
            },
        }
    )

    runtime_pointer_blob, runtime_pointer_record = read_firmware_blob(
        "firmware-scheduler-runtime-pointer",
        J713_FW_SCHED_RUNTIME_POINTER_VA,
        8,
    )
    runtime = u64(runtime_pointer_blob, 0)
    if runtime & 0xFFFFFF0000000000 != 0xFFFFFC0000000000:
        raise RuntimeError(
            f"invalid firmware scheduler runtime pointer 0x{runtime:016x}"
        )
    runtime_blob, runtime_record = read_firmware_range(
        "firmware-initdata-runtime", runtime, J713_INITDATA_RUNTIME_SIZE
    )
    # Read only the firmware-to-host trace ring. Never consume/acknowledge it.
    # 25G83 Runtime+1e0/+1e8, 512 records of 0x48 bytes; preserve both index
    # samples since native firmware and macOS continue to produce/consume.
    firmware_trace = {}
    try:
        trace_state = u64(runtime_blob, 0x1e0)
        trace_data = u64(runtime_blob, 0x1e8)
        if any(p & 0xFFFFFF0000000000 != 0xFFFFFC0000000000 or p & 3
               for p in (trace_state, trace_data)):
            raise RuntimeError("invalid firmware trace-ring pointers")
        started = time.monotonic_ns()
        before, before_record = read_firmware_range("firmware-ktrace-state-before", trace_state, 0x30)
        records, records_record = read_firmware_range("firmware-ktrace-records", trace_data, 0x9000)
        after, after_record = read_firmware_range("firmware-ktrace-state-after", trace_state, 0x30)
        firmware_trace = decode_firmware_trace(before, records, after)
        firmware_trace.update({"read_begin_monotonic_ns": started,
            "read_end_monotonic_ns": time.monotonic_ns(),
            "state_before": before_record, "records": records_record,
            "state_after": after_record})
        (directory / "firmware-ktrace.json").write_text(json.dumps(firmware_trace, indent=2) + "\n")
    except (RuntimeError, ValueError) as error:
        firmware_trace["error"] = str(error)
    firmware_brn_table = u64(runtime_blob, J713_RUNTIME_FW_BRN_TABLE_OFFSET)
    if firmware_brn_table & 0xFFFFFF0000000000 != 0xFFFFFC0000000000:
        raise RuntimeError(
            "invalid runtime +0x2c8 firmware BRN-table pointer "
            f"0x{firmware_brn_table:016x}"
        )
    firmware_brn_table_blob, firmware_brn_table_record = read_firmware_range(
        "firmware-brn-table-page",
        firmware_brn_table,
        J713_FW_BRN_TABLE_CAPTURE_SIZE,
    )
    active_firmware_brn_entries = []
    for brn_id in range(J713_FW_BRN_TABLE_ENTRY_COUNT):
        offset = brn_id * J713_FW_BRN_TABLE_ENTRY_SIZE
        words = [
            u32(firmware_brn_table_blob, offset),
            u32(firmware_brn_table_blob, offset + 4),
        ]
        if not any(words):
            continue
        active_firmware_brn_entries.append(
            {
                "brn_id": brn_id,
                # Firmware paths at 0x4424 and 0x310cc compare and clear
                # these two words. Their precise producer/consumer meaning
                # depends on the command path, so retain neutral names.
                "word0": f"0x{words[0]:08x}",
                "word1": f"0x{words[1]:08x}",
            }
        )

    pb_hardware_table_gpu = u64(
        runtime_blob, J713_RUNTIME_PB_DESCRIPTOR_GPU_OFFSET
    )
    pb_hardware_table = u64(
        runtime_blob, J713_RUNTIME_PB_DESCRIPTOR_FIRMWARE_OFFSET
    )
    if pb_hardware_table_gpu == 0 or pb_hardware_table_gpu >> 43:
        raise RuntimeError(
            "invalid PB hardware-table GPU pointer "
            f"0x{pb_hardware_table_gpu:016x}"
        )
    if pb_hardware_table & 0xFFFFFF0000000000 != 0xFFFFFC0000000000:
        raise RuntimeError(
            "invalid PB hardware-table firmware pointer "
            f"0x{pb_hardware_table:016x}"
        )
    pb_hardware_table_blob, pb_hardware_table_record = read_firmware_range(
        "firmware-parameter-buffer-hardware-table-page",
        pb_hardware_table,
        J713_PB_HARDWARE_ALLOCATION_CAPTURE_SIZE,
    )
    active_pb_hardware_descriptors = []
    for hardware_buffer_id in range(J713_PB_HARDWARE_DESCRIPTOR_COUNT):
        offset = hardware_buffer_id * J713_PB_HARDWARE_DESCRIPTOR_SIZE
        words = [
            u32(pb_hardware_table_blob, offset + word * 4)
            for word in range(4)
        ]
        if not any(words):
            continue
        active_pb_hardware_descriptors.append(
            {
                "hardware_buffer_id": hardware_buffer_id,
                "raw": [f"0x{word:08x}" for word in words],
                # These are the fields round-tripped by Apple's named G16
                # halLoadPBDesc()/halStorePBDesc() pair.  Preserve all raw
                # words because the remaining address fields are only partly
                # reconstructed.
                "active": words[0] & 1,
                "ring_head": words[2] & 0x3FFFFF,
                "ring_tail": words[3] & 0x3FFFFF,
                "ring_wrap": words[3] >> 31,
            }
        )

    uma_hardware_table_gpu = u64(
        runtime_blob, J713_RUNTIME_UMA_DESCRIPTOR_GPU_OFFSET
    )
    uma_hardware_table = u64(
        runtime_blob, J713_RUNTIME_UMA_DESCRIPTOR_FIRMWARE_OFFSET
    )
    if uma_hardware_table_gpu == 0 or uma_hardware_table_gpu >> 43:
        raise RuntimeError(
            "invalid USC private-memory hardware-table GPU pointer "
            f"0x{uma_hardware_table_gpu:016x}"
        )
    if uma_hardware_table & 0xFFFFFF0000000000 != 0xFFFFFC0000000000:
        raise RuntimeError(
            "invalid USC private-memory hardware-table pointer "
            f"0x{uma_hardware_table:016x}"
        )
    uma_hardware_table_blob, uma_hardware_table_record = read_firmware_range(
        "firmware-usc-private-memory-hardware-table",
        uma_hardware_table,
        J713_USC_PRIVATE_MEMORY_HARDWARE_TABLE_SIZE,
    )
    active_uma_hardware_descriptors = []
    for hardware_buffer_id in range(
        J713_USC_PRIVATE_MEMORY_HARDWARE_DESCRIPTOR_COUNT
    ):
        offset = (
            hardware_buffer_id
            * J713_USC_PRIVATE_MEMORY_HARDWARE_DESCRIPTOR_SIZE
        )
        words = [
            u64(uma_hardware_table_blob, offset + word * 8)
            for word in range(4)
        ]
        if not any(words):
            continue
        active_uma_hardware_descriptors.append(
            {
                "hardware_buffer_id": hardware_buffer_id,
                "raw": [f"0x{word:016x}" for word in words],
                "pool_state_gpu_va": (
                    f"0x{((words[0] & ((1 << 41) - 1)) << 7):016x}"
                ),
                "pool_state_qword_count": words[0] >> 41,
                # Firmware packs descriptor +20/+24 here (TEXT2afec).
                # Both advance beyond the pool's total page count in native
                # captures. Do not mislabel cursors as free/allocated counts.
                "ring_cursor0": (words[1] >> 5) & ((1 << 28) - 1),
                "ring_cursor1": (words[1] >> 33) & ((1 << 28) - 1),
                "empty": (words[1] >> 61) & 1,
                "total_page_count": words[2],
            }
        )
    for name, offset in [('vertex',0x254),('fragment',0x25c),('compute',0x264)]:
        engine_pointer = u64(runtime_blob, offset)
        if engine_pointer:
            read_firmware_range('firmware-global-engine-'+name, engine_pointer, 64)
    main = u64(runtime_blob, 0)
    if main & 0xFFFFFF0000000000 != 0xFFFFFC0000000000:
        raise RuntimeError(f"invalid firmware main pointer 0x{main:016x}")
    _main_blob, main_record = read_firmware_range(
        "firmware-initdata-main", main, J713_INITDATA_MAIN_SIZE
    )
    host_globals_pointer_blob, host_globals_pointer_record = read_firmware_blob(
        "firmware-host-globals-pointer",
        J713_FW_HOST_GLOBALS_POINTER_VA,
        8,
    )
    host_globals = u64(host_globals_pointer_blob, 0)
    if host_globals & 0xFFFFFF0000000000 != 0xFFFFFC0000000000:
        raise RuntimeError(
            f"invalid firmware host-globals pointer 0x{host_globals:016x}"
        )
    _host_globals_blob, host_globals_record = read_firmware_range(
        "firmware-initdata-host-globals",
        host_globals,
        J713_INITDATA_HOST_GLOBALS_SIZE,
    )
    power_data_pointer_blob, power_data_pointer_record = read_firmware_blob(
        "firmware-power-data-pointer",
        J713_FW_POWER_DATA_POINTER_VA,
        8,
    )
    power_data = u64(power_data_pointer_blob, 0)
    if power_data & 0xFFFFFF0000000000 != 0xFFFFFC0000000000:
        raise RuntimeError(
            f"invalid firmware power-data pointer 0x{power_data:016x}"
        )
    _power_data_blob, power_data_record = read_firmware_range(
        "firmware-initdata-power-data",
        power_data,
        J713_INITDATA_POWER_DATA_SIZE,
    )
    engine_work_pointer_blob, engine_work_pointer_record = read_firmware_blob(
        "firmware-scheduler-engine-work-state-pointer",
        runtime + J713_FW_SCHED_RUNTIME_ENGINE_WORK_STATE_OFFSET,
        8,
    )
    engine_work = u64(engine_work_pointer_blob, 0)
    if engine_work & 0xFFFFFF0000000000 != 0xFFFFFC0000000000:
        raise RuntimeError(
            f"invalid firmware engine-work-state pointer 0x{engine_work:016x}"
        )
    counter_blob, counter_record = read_firmware_blob(
        "firmware-engine-compute-work-counter",
        engine_work + J713_FW_ENGINE_WORK_COMPUTE_COUNTER_OFFSET,
        4,
    )

    # Capture the complete producer/consumer chain that powers a queued GPU
    # command.  The runtime pointer at +0x469 is intentionally unaligned in
    # G16 InitData.  The host counter is the first Fender-scratch allocation;
    # firmware mirrors the generation it consumed in firmware-data +0xeae8.
    power_pointer_blob, power_pointer_record = read_firmware_blob(
        "firmware-runtime-power-performance-pointer",
        runtime + J713_RUNTIME_POWER_PERFORMANCE_OFFSET,
        8,
    )
    power_performance = u64(power_pointer_blob, 0)
    if power_performance & 0xFFFFFF0000000000 != 0xFFFFFC0000000000:
        raise RuntimeError(
            "invalid firmware power-performance pointer "
            f"0x{power_performance:016x}"
        )
    power_performance_blob, power_performance_record = read_firmware_range(
        "firmware-initdata-power-performance",
        power_performance,
        J713_INITDATA_PWRPERF_SIZE,
    )
    power_performance_auxiliary = u64(
        power_performance_blob, J713_PWRPERF_AUXILIARY_POINTER_OFFSET
    )
    if (
        power_performance_auxiliary & 0xFFFFFF0000000000
        != 0xFFFFFC0000000000
    ):
        raise RuntimeError(
            "invalid power-performance +0x6318 auxiliary pointer "
            f"0x{power_performance_auxiliary:016x}"
        )
    (
        power_performance_auxiliary_blob,
        power_performance_auxiliary_record,
    ) = read_firmware_range(
        "firmware-power-performance-auxiliary-page",
        power_performance_auxiliary,
        J713_PWRPERF_AUXILIARY_CAPTURE_SIZE,
    )
    power_state_blob, power_state_record = read_firmware_blob(
        "firmware-gpu-power-performance-state",
        power_performance + J713_PWRPERF_GPU_POWER_STATE_OFFSET,
        4,
    )
    firmware_data_pointer_blob, firmware_data_pointer_record = (
        read_firmware_blob(
            "firmware-data-pointer",
            J713_FW_DATA_POINTER_VA,
            8,
        )
    )
    firmware_data = u64(firmware_data_pointer_blob, 0)
    if firmware_data & 0xFFFFFF0000000000 != 0xFFFFFC0000000000:
        raise RuntimeError(
            f"invalid firmware-data pointer 0x{firmware_data:016x}"
        )
    _firmware_data_blob, firmware_data_record = read_firmware_range(
        "firmware-initdata-firmware-data",
        firmware_data,
        J713_INITDATA_FW_DATA_SIZE,
    )
    consumed_blob, consumed_record = read_firmware_blob(
        "firmware-consumed-gpu-core-power-assertion",
        firmware_data + J713_FW_DATA_CONSUMED_GPU_CORE_POWER_ASSERT_OFFSET,
        4,
    )
    scheduler_init_blob, scheduler_init_record = read_firmware_blob(
        "firmware-scheduler-initialization-state",
        firmware_data + J713_FW_DATA_SCHEDULER_INIT_OFFSET,
        4,
    )
    scheduler_events_blob, scheduler_events_record = read_firmware_blob(
        "firmware-scheduler-events",
        J713_FW_SCHED_EVENTS_VA,
        8,
    )
    power_activation = {
        "power_performance": {
            **power_pointer_record,
            "pointer": f"0x{power_performance:016x}",
        },
        "power_performance_auxiliary": {
            **power_performance_auxiliary_record,
            "pointer_source_offset": (
                f"0x{J713_PWRPERF_AUXILIARY_POINTER_OFFSET:x}"
            ),
            "pointer": f"0x{power_performance_auxiliary:016x}",
            "nonzero_bytes": sum(
                byte != 0 for byte in power_performance_auxiliary_blob
            ),
        },
        "gpu_power_state": {
            **power_state_record,
            "value": u32(power_state_blob, 0),
        },
        "firmware_data": {
            **firmware_data_pointer_record,
            "pointer": f"0x{firmware_data:016x}",
        },
        "consumed_power_assertion_generation": {
            **consumed_record,
            "value": u32(consumed_blob, 0),
        },
        "scheduler_initialization_state": {
            **scheduler_init_record,
            "value": u32(scheduler_init_blob, 0),
        },
        "scheduler_events": {
            **scheduler_events_record,
            "value": f"0x{u64(scheduler_events_blob, 0):016x}",
        },
    }
    if capture_mmio:
        host_counter_physical = (
            J713_SGX_SECONDARY_BASE
            + J713_FENDER_SCRATCH_OFFSET
            + J713_GPU_CORE_POWER_ASSERT_COUNTER_OFFSET
        )
        host_counter_blob = devmem.read_mmio(host_counter_physical, 4)
        save_blob(
            directory,
            "fender-host-gpu-core-power-assertion",
            host_counter_physical,
            host_counter_blob,
            manifest,
        )
        gpc_state_physical = (
            J713_SGX_MAIN_BASE + J713_SGX_GPC_PERFORMANCE_STATE_OFFSET
        )
        gpc_state_blob = devmem.read_mmio(gpc_state_physical, 4)
        save_blob(
            directory,
            "sgx-gpc-performance-state",
            gpc_state_physical,
            gpc_state_blob,
            manifest,
        )
        power_activation.update(
            {
                "host_power_assertion_generation": {
                    "physical_address": f"0x{host_counter_physical:016x}",
                    "file": "fender-host-gpu-core-power-assertion.bin",
                    "value": u32(host_counter_blob, 0),
                },
                "gpc_performance_state": {
                    "physical_address": f"0x{gpc_state_physical:016x}",
                    "file": "sgx-gpc-performance-state.bin",
                    "raw": f"0x{u32(gpc_state_blob, 0):08x}",
                    "state": u32(gpc_state_blob, 0) & 0xF,
                },
            }
        )
    else:
        power_activation["mmio_capture"] = "disabled-by-firmware-only-mode"
    return {
        "firmware_trace": firmware_trace,
        "engine_state": engine_record,
        "iomap_state": iomap_record,
        "scheduler_runtime": {
            **runtime_pointer_record,
            "pointer": f"0x{runtime:016x}",
        },
        "initdata_objects": {
            "runtime": runtime_record,
            "main": {
                **main_record,
                "pointer": f"0x{main:016x}",
            },
            "host_globals": {
                **host_globals_record,
                "pointer_source": host_globals_pointer_record,
                "pointer": f"0x{host_globals:016x}",
            },
            "power_data": {
                **power_data_record,
                "pointer_source": power_data_pointer_record,
                "pointer": f"0x{power_data:016x}",
            },
            "power_performance": power_performance_record,
            "power_performance_auxiliary": {
                **power_performance_auxiliary_record,
                "pointer_source_offset": (
                    f"0x{J713_PWRPERF_AUXILIARY_POINTER_OFFSET:x}"
                ),
                "pointer": f"0x{power_performance_auxiliary:016x}",
                "nonzero_bytes": sum(
                    byte != 0 for byte in power_performance_auxiliary_blob
                ),
            },
            "firmware_data": firmware_data_record,
        },
        "firmware_brn_table": {
            **firmware_brn_table_record,
            "structure": "g16_firmware_brn_counter_pair_table",
            "producer": "AGXAcceleratorG16::populateFWBRNTable(void *)",
            "pointer": f"0x{firmware_brn_table:016x}",
            "entry_size": J713_FW_BRN_TABLE_ENTRY_SIZE,
            "entry_count": J713_FW_BRN_TABLE_ENTRY_COUNT,
            "decoded_size": J713_FW_BRN_TABLE_DECODED_SIZE,
            "active_entries": active_firmware_brn_entries,
            "nonzero_bytes": sum(
                byte != 0 for byte in firmware_brn_table_blob
            ),
        },
        "parameter_buffer_hardware_table": {
            **pb_hardware_table_record,
            "structure": "g16_parameter_buffer_hardware_descriptor_table",
            "gpu_pointer": f"0x{pb_hardware_table_gpu:016x}",
            "firmware_pointer": f"0x{pb_hardware_table:016x}",
            "descriptor_size": J713_PB_HARDWARE_DESCRIPTOR_SIZE,
            "descriptor_count": J713_PB_HARDWARE_DESCRIPTOR_COUNT,
            "decoded_size": J713_PB_HARDWARE_TABLE_SIZE,
            "active_descriptors": active_pb_hardware_descriptors,
        },
        "usc_private_memory_hardware_table": {
            **uma_hardware_table_record,
            "structure": "g16_usc_private_memory_hardware_descriptor_table",
            "gpu_pointer": f"0x{uma_hardware_table_gpu:016x}",
            "firmware_pointer": f"0x{uma_hardware_table:016x}",
            # Keep the historical key for consumers of the first oracle.
            "pointer": f"0x{uma_hardware_table:016x}",
            "descriptor_size": J713_USC_PRIVATE_MEMORY_HARDWARE_DESCRIPTOR_SIZE,
            "descriptor_count": J713_USC_PRIVATE_MEMORY_HARDWARE_DESCRIPTOR_COUNT,
            "active_descriptors": active_uma_hardware_descriptors,
        },
        "engine_work_state": {
            **engine_work_pointer_record,
            "pointer": f"0x{engine_work:016x}",
        },
        "compute_work_counter": {
            **counter_record,
            "value": u32(counter_blob, 0),
        },
        "power_activation": power_activation,
    }


def snapshot_uat_context_tables(
    devmem: DevMem,
    directory: Path,
    manifest: dict,
    context_id: int,
    ttbr0: int,
    ttbr1: int,
) -> dict:
    """Preserve a selected context's complete three-level table topology.

    Resource walks only retain tables on paths containing known producer
    mappings.  G16 user contexts also contain private, implicit ranges which
    are not represented in the IOGPU resource namespace.  Save both roots,
    every valid L1 child, and every valid L2 child, with a conservative bound
    so a corrupt descriptor cannot turn a live gate into an unbounded scan.
    """

    page_limit = 256
    pages: dict[int, bytes] = {}
    dummy_gart = None

    def read_table(address: int) -> bytes:
        if address not in pages:
            if len(pages) >= page_limit:
                raise RuntimeError(
                    f"context {context_id} UAT topology exceeds {page_limit} pages"
                )
            pages[address] = devmem.read_physical(address, 0x4000)
        name = f"uat-table-{address:016x}"
        if name not in manifest["blobs"]:
            save_blob(directory, name, address, pages[address], manifest)
        return pages[address]

    roots = []
    for selector, ttbr in enumerate((ttbr0, ttbr1)):
        root_address = ttbr & J713_UAT_PHYS_MASK
        root_record = {
            "address_space": f"TTBR{selector}",
            "ttbr": f"0x{ttbr:016x}",
            "valid_l1_entries": [],
        }
        roots.append(root_record)
        if root_address == 0 or not (ttbr & 1):
            continue
        root = read_table(root_address)
        root_record["file"] = f"uat-table-{root_address:016x}.bin"
        for l1_index in range(64):
            l1_entry = u64(root, l1_index * 8)
            if (l1_entry & 3) != 3:
                continue
            l2_address = l1_entry & J713_UAT_PHYS_MASK
            if l2_address == 0:
                continue
            l2 = read_table(l2_address)
            l1_record = {
                "index": l1_index,
                "entry": f"0x{l1_entry:016x}",
                "file": f"uat-table-{l2_address:016x}.bin",
                "valid_l2_entries": [],
            }
            root_record["valid_l1_entries"].append(l1_record)
            for l2_index in range(0x800):
                l2_entry = u64(l2, l2_index * 8)
                if (l2_entry & 3) != 3:
                    continue
                l3_address = l2_entry & J713_UAT_PHYS_MASK
                if l3_address == 0:
                    continue
                l3 = read_table(l3_address)
                l1_record["valid_l2_entries"].append(
                    {
                        "index": l2_index,
                        "entry": f"0x{l2_entry:016x}",
                        "file": f"uat-table-{l3_address:016x}.bin",
                    }
                )
                dummy_l1 = (
                    J713_UAT_DUMMY_GART_VA >> J713_UAT_LEVEL_SHIFTS[0]
                ) & J713_UAT_LEVEL_INDEX_MASKS[0]
                dummy_l2 = (
                    J713_UAT_DUMMY_GART_VA >> J713_UAT_LEVEL_SHIFTS[1]
                ) & J713_UAT_LEVEL_INDEX_MASKS[1]
                dummy_l3 = (
                    J713_UAT_DUMMY_GART_VA >> J713_UAT_LEVEL_SHIFTS[2]
                ) & J713_UAT_LEVEL_INDEX_MASKS[2]
                if (
                    selector == 0
                    and l1_index == dummy_l1
                    and l2_index == dummy_l2
                ):
                    leaf = u64(l3, dummy_l3 * 8)
                    if leaf & 1:
                        physical_address = leaf & J713_UAT_PHYS_MASK
                        name = f"uat-dummy-gart-{physical_address:016x}"
                        payload = devmem.read_physical(physical_address, 0x4000)
                        if name not in manifest["blobs"]:
                            save_blob(
                                directory,
                                name,
                                physical_address,
                                payload,
                                manifest,
                            )
                        dummy_gart = {
                            "gpu_va": f"0x{J713_UAT_DUMMY_GART_VA:016x}",
                            "l1_index": dummy_l1,
                            "l2_index": dummy_l2,
                            "l3_index": dummy_l3,
                            "leaf": f"0x{leaf:016x}",
                            "physical_address": f"0x{physical_address:016x}",
                            "file": f"{name}.bin",
                            "nonzero_bytes": sum(byte != 0 for byte in payload),
                        }
    result = {
        "context_id": context_id,
        "page_limit": page_limit,
        "page_count": len(pages),
        "roots": roots,
    }
    if dummy_gart is not None:
        result["dummy_gart"] = dummy_gart
    return result


def snapshot_sgx_mmio_state(
    devmem: DevMem, directory: Path, manifest: dict
) -> dict:
    """Capture read-only SGX launch registers while a successful job is live."""

    regions = {}
    payloads = {}
    for name, offset, length in J713_SGX_MMIO_REGIONS:
        address = J713_SGX_MAIN_BASE + offset
        payload = devmem.read_mmio(address, length)
        save_blob(directory, name, address, payload, manifest)
        payloads[name] = payload
        regions[name] = {
            "address": f"0x{address:016x}",
            "length": length,
            "file": f"{name}.bin",
        }

    applied_compute_registers = []
    for role, offset in J713_SGX_COMPUTE_DESTINATION_REGISTERS:
        address = J713_SGX_MAIN_BASE + offset
        payload = devmem.read_mmio(address, 8)
        name = f"sgx-applied-{role}"
        save_blob(directory, name, address, payload, manifest)
        applied_compute_registers.append(
            {
                "role": role,
                "register": f"0x{offset:05x}",
                "address": f"0x{address:016x}",
                "value": f"0x{u64(payload, 0):016x}",
                "file": f"{name}.bin",
            }
        )

    issue = payloads["sgx-engine-issue"]
    register_list = u64(issue, 0x08)
    auxiliary = payloads["sgx-engine-auxiliary"]
    result = {
        "capture_boundary": "successful-compute-executing-shader-gate",
        "regions": regions,
        "applied_compute_registers": applied_compute_registers,
        "engine_issue": {
            "dependency_slot_mask": f"0x{u64(issue, 0x00):016x}",
            "register_list_base_and_count": f"0x{register_list:016x}",
            "register_list_base": f"0x{register_list & ((1 << 44) - 1):016x}",
            "register_count": register_list >> 48,
            "issue_control": f"0x{u64(issue, 0x10):016x}",
            "selected_slot_mask": f"0x{u64(issue, 0x18):016x}",
        },
        "engine_auxiliary": [
            {
                "slot": slot,
                "value": f"0x{value:016x}",
                "base": f"0x{value & ((1 << 44) - 1):016x}",
                "count_minus_one": value >> 48,
            }
            for slot in range(J713_SGX_ENGINE_AUXILIARY_SIZE // 8)
            if (value := u64(auxiliary, slot * 8)) != 0
        ],
        "hardware_queue_status": f"0x{u64(payloads['sgx-hardware-queue-status'], 0):016x}",
        "interrupt_status_words": [
            f"0x{u64(payloads['sgx-interrupt-status'], offset):016x}"
            for offset in range(0, J713_SGX_INTERRUPT_STATUS_SIZE, 8)
        ],
        "global_table_registers": {
            "parameter_buffer_base": (
                f"0x{u64(payloads['sgx-global-parameter-buffer-base'], 0):016x}"
            ),
            "usc_private_memory_base": (
                f"0x{u64(payloads['sgx-global-usc-private-memory-base'], 0):016x}"
            ),
        },
        "shader_address_register_hypotheses": {
            "usc_exec_base_cp_g14x": (
                f"0x{u64(payloads['sgx-usc-exec-base-cp-g14x-hypothesis'], 0):016x}"
            ),
            "usc_helper_program_g14x": (
                f"0x{u64(payloads['sgx-usc-helper-program-g14x-hypothesis'], 0):016x}"
            ),
            "usc_helper_argument_g14x": (
                f"0x{u64(payloads['sgx-usc-helper-argument-g14x-hypothesis'], 0):016x}"
            ),
            "usc_helper_config_g14x": (
                f"0x{u64(payloads['sgx-usc-helper-config-g14x-hypothesis'], 0):016x}"
            ),
            "usc_iogpu_mode_g14x": (
                f"0x{u64(payloads['sgx-usc-iogpu-mode-g14x-hypothesis'], 0):016x}"
            ),
        },
    }

    # The engine does not consume the RunCompute command's producer-side list
    # directly on a successful G16 launch.  Firmware publishes a second list
    # here and clears its backing storage after completion, so preserve the
    # physical pages synchronously while commandprobe holds the shader gate.
    context_ids = []
    for queue in manifest.get("compute_queue_objects", {}).get("objects", []):
        for command in queue.get("command_ring", {}).get("commands", []):
            firmware_file = command.get("firmware_command_file")
            if not firmware_file:
                continue
            firmware_path = directory / firmware_file
            if not firmware_path.exists():
                continue
            firmware = firmware_path.read_bytes()
            if len(firmware) >= 0x10:
                context_ids.append(u32(firmware, 0x0C))
    context_ids = sorted(set(context_ids))

    def capture_register_program(
        role: str, gpu_va: int, record_count: int
    ) -> dict:
        record = {
            "role": role,
            "gpu_va": f"0x{gpu_va:016x}",
            "record_count": record_count,
            "record_size_hypothesis": J713_SGX_ENGINE_REGISTER_RECORD_SIZE,
            "context_candidates": context_ids,
            "uat_candidates": [],
        }
        if gpu_va == 0:
            record["error"] = "null engine register-program pointer"
            return record
        if not 0 < record_count <= J713_SGX_ENGINE_REGISTER_CAPTURE_MAX_RECORDS:
            record["error"] = (
                f"record count {record_count} exceeds finite capture range "
                f"1..{J713_SGX_ENGINE_REGISTER_CAPTURE_MAX_RECORDS}"
            )
            return record
        if not context_ids:
            record["error"] = "no RunCompute UAT context in live queue capture"
            return record

        byte_length = record_count * J713_SGX_ENGINE_REGISTER_RECORD_SIZE
        record["byte_length_hypothesis"] = byte_length
        first_page_va = gpu_va & ~J713_UAT_PAGE_MASK
        last_page_va = (gpu_va + byte_length - 1) & ~J713_UAT_PAGE_MASK
        for context_id in context_ids:
            candidate = {"context_id": context_id, "pages": []}
            try:
                for page_va in range(first_page_va, last_page_va + 1, 0x4000):
                    walk = walk_uat_physical(devmem, context_id, page_va)
                    physical_page = int(walk["physical_address"], 16)
                    physical_page &= ~J713_UAT_PAGE_MASK
                    name = f"sgx-engine-register-page-{physical_page:016x}"
                    if name not in manifest["blobs"]:
                        save_blob(
                            directory,
                            name,
                            physical_page,
                            devmem.read_physical(physical_page, 0x4000),
                            manifest,
                        )
                    candidate["pages"].append(
                        {
                            "gpu_page": f"0x{page_va:016x}",
                            "physical_page": f"0x{physical_page:016x}",
                            "file": f"{name}.bin",
                            "uat_walk": walk,
                        }
                    )
            except RuntimeError as error:
                candidate["error"] = str(error)
            record["uat_candidates"].append(candidate)
        return record

    programs = [
        capture_register_program(
            "primary",
            register_list & ((1 << 44) - 1),
            register_list >> 48,
        )
    ]
    for slot in result["engine_auxiliary"]:
        programs.append(
            capture_register_program(
                f"auxiliary-slot-{slot['slot']}",
                int(slot["base"], 16),
                slot["count_minus_one"] + 1,
            )
        )
    result["engine_register_programs"] = programs

    issue_after = devmem.read_mmio(
        J713_SGX_MAIN_BASE + J713_SGX_ENGINE_ISSUE_OFFSET,
        J713_SGX_ENGINE_ISSUE_SIZE,
    )
    auxiliary_after = devmem.read_mmio(
        J713_SGX_MAIN_BASE + J713_SGX_ENGINE_AUXILIARY_OFFSET,
        J713_SGX_ENGINE_AUXILIARY_SIZE,
    )
    save_blob(
        directory,
        "sgx-engine-issue-after-register-capture",
        J713_SGX_MAIN_BASE + J713_SGX_ENGINE_ISSUE_OFFSET,
        issue_after,
        manifest,
    )
    save_blob(
        directory,
        "sgx-engine-auxiliary-after-register-capture",
        J713_SGX_MAIN_BASE + J713_SGX_ENGINE_AUXILIARY_OFFSET,
        auxiliary_after,
        manifest,
    )
    result["engine_pointer_snapshot_stable"] = (
        issue_after == issue and auxiliary_after == auxiliary
    )
    result["engine_issue_after_file"] = (
        "sgx-engine-issue-after-register-capture.bin"
    )
    result["engine_auxiliary_after_file"] = (
        "sgx-engine-auxiliary-after-register-capture.bin"
    )
    return result


def snapshot_physical_gpu_state(
    devmem: DevMem,
    directory: Path,
    manifest: dict,
    resource_manifest: dict | None = None,
    capture_usc_private_memory: bool = False,
    capture_mmio: bool = False,
) -> None:
    """Capture the live J713 handoff/TTBAT and any selected compute SKU."""

    state = manifest.setdefault("physical_gpu_state", {})
    if (
        capture_mmio
        and manifest.get("stage") == "executing"
        and "sgx_mmio" not in state
    ):
        try:
            state["sgx_mmio"] = snapshot_sgx_mmio_state(
                devmem, directory, manifest
            )
        except RuntimeError as error:
            state["sgx_mmio_error"] = str(error)
    if (
        manifest.get("stage") in {"executing", "complete"}
        and "firmware_scheduler_launch" not in state
    ):
        try:
            state["firmware_scheduler_launch"] = (
                snapshot_firmware_scheduler_launch_state(
                    devmem, directory, manifest, capture_mmio=capture_mmio
                )
            )
        except RuntimeError as error:
            state["firmware_scheduler_launch_error"] = str(error)
    if "firmware_context_generation" not in state:
        try:
            state["firmware_context_generation"] = (
                snapshot_firmware_context_generation_state(
                    devmem, directory, manifest
                )
            )
        except RuntimeError as error:
            # Keep the finite live-shader gate releasable while retaining the
            # precise failed walk as evidence in the manifest.
            state["firmware_context_generation_error"] = str(error)
    if "gpu-handoff" not in manifest["blobs"]:
        handoff = devmem.read_physical(
            J713_GFX_HANDOFF_BASE, J713_GFX_HANDOFF_USED_SIZE
        )
        ttbat = devmem.read_physical(
            J713_GPU_REGION_BASE, J713_UAT_TTBAT_USED_SIZE
        )
        save_blob(
            directory,
            "gpu-handoff",
            J713_GFX_HANDOFF_BASE,
            handoff,
            manifest,
        )
        save_blob(
            directory,
            "uat-ttbat",
            J713_GPU_REGION_BASE,
            ttbat,
            manifest,
        )
        state.update(
            {
                "handoff_used_size": J713_GFX_HANDOFF_USED_SIZE,
                "magic_ap": f"0x{u64(handoff, 0x00):016x}",
                "magic_fw": f"0x{u64(handoff, 0x08):016x}",
                "lock_ap": handoff[0x10],
                "lock_fw": handoff[0x11],
                "turn": u32(handoff, 0x14),
                "current_context": u32(handoff, 0x18),
                "firmware_attention": handoff[0x638],
                "unknown_640": f"0x{u64(handoff, 0x640):016x}",
                "contexts": [
                    {
                        "id": context,
                        "ttbr0": f"0x{u64(ttbat, context * 16):016x}",
                        "ttbr1": f"0x{u64(ttbat, context * 16 + 8):016x}",
                    }
                    for context in range(J713_UAT_CONTEXT_COUNT)
                ],
            }
        )

    context_ids = set()
    for queue in manifest.get("compute_queue_objects", {}).get("objects", []):
        for command in queue.get("command_ring", {}).get("commands", []):
            firmware_file = command.get("firmware_command_file")
            if firmware_file:
                firmware = (directory / firmware_file).read_bytes()
                context_ids.add(u32(firmware, 0x0C))

    # Render-only captures have no compute descriptor from which to infer
    # the client context. Use each backlink-validated render command instead.
    for queue in manifest.get("render_queue_objects", {}).get("objects", []):
        for command in queue.get("commands", []):
            for firmware in command.get("firmware_objects", []):
                if firmware.get("command_identity_valid", False):
                    context_ids.add(firmware["context_id"])

    context_table_snapshots = state.setdefault("context_uat_tables", [])
    snapshotted_contexts = {
        snapshot["context_id"] for snapshot in context_table_snapshots
    }
    for context_id in sorted(context_ids - snapshotted_contexts):
        context = state["contexts"][context_id]
        context_table_snapshots.append(
            snapshot_uat_context_tables(
                devmem,
                directory,
                manifest,
                context_id,
                int(context["ttbr0"], 16),
                int(context["ttbr1"], 16),
            )
        )

    resource_walks = state.setdefault("resource_uat_walks", [])
    walked_resources = {
        (walk["context_id"], walk["resource_id"], walk["mapping_index"])
        for walk in resource_walks
    }
    resources = resource_manifest if resource_manifest is not None else manifest
    for context_id in sorted(context_ids):
        for resource in resources.get("namespaces", {}).get(
            "resources", {}
        ).get("objects", []):
            for mapping in resource.get("mappings", []):
                identity = (context_id, resource["id"], mapping["index"])
                if identity in walked_resources:
                    continue
                result = {
                    "context_id": context_id,
                    "resource_id": resource["id"],
                    "mapping_index": mapping["index"],
                    "gpu_va": mapping["gpu_va"],
                }
                try:
                    walk = walk_uat_physical(
                        devmem, context_id, int(mapping["gpu_va"], 16)
                    )
                    result["walk"] = walk
                    result["table_files"] = save_uat_walk_tables(
                        devmem, directory, manifest, walk
                    )
                except RuntimeError as error:
                    result["error"] = str(error)
                resource_walks.append(result)
                walked_resources.add(identity)

    for queue in manifest.get("compute_queue_objects", {}).get("objects", []):
        for command in queue.get("command_ring", {}).get("commands", []):
            firmware_file = command.get("firmware_command_file")
            firmware_gpu_text = command.get("firmware_command_gpu")
            if (
                firmware_file
                and firmware_gpu_text
                and not command.get("firmware_command_uat_walk")
            ):
                firmware = (directory / firmware_file).read_bytes()
                context_id = u32(firmware, 0x0C)
                firmware_gpu = int(firmware_gpu_text, 16)
                try:
                    walk = walk_uat_physical(
                        devmem, context_id, firmware_gpu
                    )
                    command["firmware_command_uat_walk"] = walk
                    command["firmware_command_uat_table_files"] = (
                        save_uat_walk_tables(
                            devmem, directory, manifest, walk
                        )
                    )
                except RuntimeError as error:
                    command["firmware_command_uat_error"] = str(error)
            if not firmware_file:
                continue
            firmware = (directory / firmware_file).read_bytes()
            context_id = u32(firmware, 0x0C)
            producer_sku_file = command.get("sku_stream_file")
            if (
                producer_sku_file
                and command.get("sku_identity_valid", False)
                and not command.get("referenced_compute_pages")
            ):
                producer_sku = (directory / producer_sku_file).read_bytes()
                if len(producer_sku) >= 0x1A8:
                    snapshot_compute_referenced_pages(
                        devmem,
                        directory,
                        manifest,
                        command,
                        firmware,
                        producer_sku,
                        context_id,
                        capture_usc_private_memory=capture_usc_private_memory,
                    )
            if command.get("sku_stream_uat_file"):
                continue
            sku_gpu = u64(firmware, RUN_COMPUTE_SKU_POINTER_OFFSET)
            sku_size = u32(firmware, RUN_COMPUTE_SKU_SIZE_OFFSET)
            if sku_gpu == 0 or not 0 < sku_size <= 0x10000:
                continue
            try:
                walk = (walk_firmware_uat_physical(devmem, sku_gpu)
                        if sku_gpu & 0xffffff0000000000 == 0xfffffc0000000000
                        else walk_uat_physical(devmem, context_id, sku_gpu))
            except RuntimeError as error:
                # The independent physical walk is corroborating evidence,
                # not permission to strand the finite live-shader gate.  The
                # producer-side AGXSKUEncoder buffer has already been saved.
                command["sku_uat_error"] = str(error)
                continue
            physical = int(walk["physical_address"], 16)
            sku = bytearray()
            walks, table_files = [], []
            while len(sku) < sku_size:
                address = sku_gpu + len(sku)
                page_walk = (walk_firmware_uat_physical(devmem, address)
                             if address & 0xffffff0000000000 == 0xfffffc0000000000
                             else walk_uat_physical(devmem, context_id, address))
                page_pa = int(page_walk["physical_address"], 16)
                length = min(sku_size-len(sku), 0x4000-(page_pa & 0x3fff))
                sku.extend(devmem.read_physical(page_pa, length))
                walks.append(page_walk)
                table_files.extend(save_uat_walk_tables(devmem, directory, manifest, page_walk))
            sku = bytes(sku)
            name = (
                f"queue-{queue['queue_id']:05d}-command-"
                f"{command['counter']:08x}-sku-stream-uat"
            )
            save_blob(directory, name, physical, sku, manifest)
            manifest["blobs"][name]["address_space"] = "ordered-uat-pages"
            manifest["blobs"][name]["gpu_va"] = hex(sku_gpu)
            manifest["blobs"][name]["walks"] = walks
            command["sku_uat_walk"] = walk
            command["sku_uat_walks"] = walks
            command["sku_uat_table_files"] = table_files
            command["sku_stream_uat_file"] = f"{name}.bin"
            physical_identity = (u64(sku, 0x14) == int(command['firmware_companion_gpu'], 16) + 0x20
                                 and u32(sku, 0x2c) == context_id)
            command['physical_sku_identity_valid'] = physical_identity
            if physical_identity and not command.get("referenced_compute_pages"):
                snapshot_compute_referenced_pages(
                    devmem,
                    directory,
                    manifest,
                    command,
                    firmware,
                    sku,
                    context_id,
                )


def snapshot_namespace(
    devmem: DevMem, directory: Path, label: str, address: int, manifest: dict
) -> None:
    if not valid_kva(address):
        manifest["namespaces"][label] = {"address": f"0x{address:016x}", "valid": False}
        return
    header = devmem.read(address, 0x30)
    handles = u64(header, NAMESPACE_HANDLES)
    capacity = u32(header, NAMESPACE_CAPACITY)
    info = {
        "address": f"0x{address:016x}",
        "handles": f"0x{handles:016x}",
        "capacity": capacity,
        "objects": [],
    }
    manifest["namespaces"][label] = info
    save_blob(directory, f"{label}-namespace", address, header, manifest)
    if not valid_kva(handles) or capacity == 0 or capacity > 65536:
        info["valid"] = False
        return
    table = devmem.read(handles, capacity * 8)
    save_blob(directory, f"{label}-handles", handles, table, manifest)
    for object_id in range(1, capacity):
        pointer = u64(table, object_id * 8)
        if not valid_kva(pointer):
            continue
        blob_length = 0x1000 if label == "command-queues" else 0x200
        blob = devmem.read(pointer, blob_length)
        name = f"{label}-{object_id:05d}"
        save_blob(directory, name, pointer, blob, manifest)
        info["objects"].append(
            {"id": object_id, "address": f"0x{pointer:016x}", "file": f"{name}.bin"}
        )
        if label == "resources":
            snapshot_resource(devmem, directory, object_id, pointer, blob, info, manifest)
        elif label == "command-queues":
            queue = info["objects"][-1]
            queue.update(
                {
                    "scheduler_index": u32(blob, QUEUE_SCHEDULER_INDEX),
                    "gpu_task": f"0x{u64(blob, QUEUE_GPU_TASK):016x}",
                    "device": f"0x{u64(blob, QUEUE_DEVICE):016x}",
                    "pid": u32(blob, QUEUE_PID),
                    "i_gpu": f"0x{u64(blob, QUEUE_IOGPU):016x}",
                    "scheduler": f"0x{u64(blob, QUEUE_SCHEDULER):016x}",
                    "unique_id": f"0x{u64(blob, QUEUE_UNIQUE_ID):016x}",
                }
            )


def snapshot_compute_queue_objects(
    devmem: DevMem,
    directory: Path,
    manifest: dict,
    expect_compute_tail: bool = False,
    include_inactive: bool = False,
) -> None:
    """Preserve exact-size, statically proven compute workqueue objects.

    A normal one-command probe follows only the active workqueue.  A gated
    microprogram deliberately has a finite-spin command active while the
    unchanged program is pending on another persistent workqueue, so its live
    snapshot selects the single backlink-validated inactive workqueue.
    """

    queues = manifest["namespaces"]["command-queues"].get("objects", [])
    records = []
    for queue in queues:
        blob = (directory / queue["file"]).read_bytes()
        queue_address = int(queue["address"], 16)
        active_channel = u64(blob, QUEUE_ACTIVE_CL_CHANNEL)
        active_work_queue = u64(blob, QUEUE_ACTIVE_CL_WORK_QUEUE)
        protection = u64(blob, QUEUE_PROTECTION_OPTIONS)
        count = u32(blob, QUEUE_CL_WORK_QUEUE_COUNT)
        capacity = u32(blob, QUEUE_CL_WORK_QUEUE_CAPACITY)
        candidates = []
        if count <= capacity <= 64:
            for index in range(count):
                candidates.append(
                    (u64(blob, QUEUE_CL_WORK_QUEUES + index * 8),
                     f"ordinary[{index}]")
                )
        protected_index = {1: 0, 3: 1, 7: 2}.get(protection)
        if protected_index is not None:
            candidates.append(
                (u64(blob, QUEUE_PROTECTED_CL_WORK_QUEUES + protected_index * 8),
                 f"protected[{protected_index}]")
            )
        if active_work_queue and all(active_work_queue != item[0] for item in candidates):
            candidates.append((active_work_queue, "active-transient"))
        if active_work_queue:
            selected = [
                item
                for item in candidates
                if (item[0] != active_work_queue) == include_inactive
            ]
            if selected:
                candidates = selected
        if len(candidates) > 1:
            records.append(
                {
                    "queue_id": queue["id"],
                    "queue_address": queue["address"],
                    "ordinary_work_queue_count": count,
                    "ordinary_work_queue_capacity": capacity,
                    "protection_options": protection,
                    "active_channel_address": f"0x{active_channel:016x}",
                    "active_work_queue_address": f"0x{active_work_queue:016x}",
                    "valid": False,
                    "reason": (
                        "multiple persistent workqueues and no exact "
                        f"{'inactive' if include_inactive else 'active'} selection"
                    ),
                }
            )
            continue
        if not candidates:
            records.append(
                {
                    "queue_id": queue["id"],
                    "queue_address": queue["address"],
                    "ordinary_work_queue_count": count,
                    "ordinary_work_queue_capacity": capacity,
                    "protection_options": protection,
                    "active_channel_address": f"0x{active_channel:016x}",
                    "active_work_queue_address": f"0x{active_work_queue:016x}",
                    "valid": False,
                }
            )
            continue
        for work_queue, selection_source in candidates:
            is_active = active_work_queue == work_queue
            record = {
                "queue_id": queue["id"],
                "queue_address": queue["address"],
                "selection_source": selection_source,
                "ordinary_work_queue_count": count,
                "ordinary_work_queue_capacity": capacity,
                "protection_options": protection,
                "active_channel_address": f"0x{active_channel:016x}",
                "active_work_queue_address": f"0x{active_work_queue:016x}",
                "work_queue_address": f"0x{work_queue:016x}",
                "is_active_work_queue": is_active,
            }
            records.append(record)
            if not valid_graph_kva(work_queue):
                record["valid"] = False
                continue
            work_queue_blob = devmem.read(work_queue, AGX_CL_WORK_QUEUE_SIZE)
            channel = u64(work_queue_blob, CL_WORK_QUEUE_CHANNEL)
            record["channel_address"] = f"0x{channel:016x}"
            if not valid_graph_kva(channel):
                record["valid"] = False
                continue
            channel_blob = devmem.read(channel, AGX_CL_CHANNEL_G16_SIZE)
            suffix = selection_source.replace("[", "-").replace("]", "")
            channel_name = f"queue-{queue['id']:05d}-{suffix}-agx-cl-channel-g16"
            work_queue_name = f"queue-{queue['id']:05d}-{suffix}-agx-cl-work-queue"
            save_blob(directory, channel_name, channel, channel_blob, manifest)
            save_blob(directory, work_queue_name, work_queue, work_queue_blob, manifest)
            record.update(
                {
                    "channel_file": f"{channel_name}.bin",
                    "work_queue_file": f"{work_queue_name}.bin",
                    "channel_work_queue": f"0x{u64(channel_blob, CL_CHANNEL_WORK_QUEUE):016x}",
                    "channel_command_queue": f"0x{u64(channel_blob, CL_CHANNEL_COMMAND_QUEUE):016x}",
                    "work_queue_channel": f"0x{channel:016x}",
                    "valid": (
                        u64(channel_blob, CL_CHANNEL_WORK_QUEUE) == work_queue
                        and u64(channel_blob, CL_CHANNEL_COMMAND_QUEUE) == queue_address
                        and (
                            not is_active
                            or active_channel in (0, channel)
                        )
                    ),
                }
            )
            completed_head = u32(work_queue_blob, IOGPU_WORK_QUEUE_COMPLETED_HEAD)
        submitted_tail = u32(work_queue_blob, IOGPU_WORK_QUEUE_SUBMITTED_TAIL)
        enqueued_tail = u32(work_queue_blob, IOGPU_WORK_QUEUE_ENQUEUED_TAIL)
        capacity = u32(work_queue_blob, IOGPU_WORK_QUEUE_RING_CAPACITY)
        command_ring = u64(work_queue_blob, IOGPU_WORK_QUEUE_COMMAND_RING)
        occupied_count = (enqueued_tail - completed_head) & 0xFFFFFFFF
        submitted_count = (submitted_tail - completed_head) & 0xFFFFFFFF
        record["command_ring"] = {
            "completed_head": completed_head,
            "submitted_tail": submitted_tail,
            "enqueued_tail": enqueued_tail,
            "capacity": capacity,
            "address": f"0x{command_ring:016x}",
            "occupied_count": occupied_count,
            "submitted_count": submitted_count,
            "commands": [],
        }
        ring_record = record["command_ring"]
        if (
            not record["valid"]
            or capacity == 0
            or capacity > 4096
            or occupied_count > capacity
            or submitted_count > occupied_count
            or not valid_graph_kva(command_ring)
        ):
            ring_record["valid"] = False
            continue
        ring_blob = devmem.read(command_ring, capacity * 8)
        ring_name = f"queue-{queue['id']:05d}-iogpu-command-ring"
        save_blob(directory, ring_name, command_ring, ring_blob, manifest)
        ring_record["file"] = f"{ring_name}.bin"
        ring_record["valid"] = True
        for sequence in range(occupied_count):
            counter = (completed_head + sequence) & 0xFFFFFFFF
            slot = counter % capacity
            descriptor = u64(ring_blob, slot * 8)
            command = {
                "counter": counter,
                "slot": slot,
                "state": "submitted" if sequence < submitted_count else "enqueued",
                "address": f"0x{descriptor:016x}",
            }
            ring_record["commands"].append(command)
            if not valid_graph_kva(descriptor):
                command["valid"] = False
                continue
            header = devmem.read(descriptor, IOGPU_COMMAND_DESCRIPTOR_SIZE)
            header_name = (
                f"queue-{queue['id']:05d}-command-{counter:08x}-iogpu-descriptor"
            )
            save_blob(directory, header_name, descriptor, header, manifest)
            command.update(
                {
                    "header_file": f"{header_name}.bin",
                    "work_queue": f"0x{u64(header, IOGPU_COMMAND_DESCRIPTOR_WORK_QUEUE):016x}",
                    "command_queue": f"0x{u64(header, IOGPU_COMMAND_DESCRIPTOR_COMMAND_QUEUE):016x}",
                    "valid": (
                        u64(header, IOGPU_COMMAND_DESCRIPTOR_WORK_QUEUE) == work_queue
                        and u64(header, IOGPU_COMMAND_DESCRIPTOR_COMMAND_QUEUE)
                        == queue_address
                    ),
                }
            )
            is_last_submitted = sequence + 1 == submitted_count
            if not (
                expect_compute_tail
                and is_last_submitted
                and command["valid"]
            ):
                continue
            descriptor_blob = devmem.read(descriptor, AGX_CL_COMMAND_DESCRIPTOR_SIZE)
            descriptor_name = (
                f"queue-{queue['id']:05d}-command-{counter:08x}-agx-cl-descriptor"
            )
            save_blob(
                directory, descriptor_name, descriptor, descriptor_blob, manifest
            )
            firmware_cpu = u64(
                descriptor_blob, AGX_CL_DESCRIPTOR_FIRMWARE_COMMAND_CPU
            )
            firmware_gpu = u64(
                descriptor_blob, AGX_CL_DESCRIPTOR_FIRMWARE_COMMAND_GPU
            )
            companion_gpu = u64(
                descriptor_blob, AGX_CL_DESCRIPTOR_FIRMWARE_COMPANION_GPU
            )
            sku_encoder = u64(
                descriptor_blob, AGX_CL_DESCRIPTOR_SKU_ENCODER
            )
            command.update(
                {
                    "descriptor_file": f"{descriptor_name}.bin",
                    "descriptor_size": AGX_CL_COMMAND_DESCRIPTOR_SIZE,
                    "expansion_basis": (
                        "last submitted (not merely enqueued) descriptor for a "
                        "compute-only probe; "
                        "AGXCommandQueue::processCompute appends compute last"
                    ),
                    "firmware_command_cpu": f"0x{firmware_cpu:016x}",
                    "firmware_command_gpu": f"0x{firmware_gpu:016x}",
                    "firmware_companion_gpu": f"0x{companion_gpu:016x}",
                    "sku_encoder": f"0x{sku_encoder:016x}",
                }
            )
            if not valid_graph_kva(firmware_cpu) or firmware_gpu == 0:
                continue
            firmware_blob = devmem.read(firmware_cpu, RUN_COMPUTE_CAPTURE_SIZE)
            firmware_name = (
                f"queue-{queue['id']:05d}-command-{counter:08x}-run-compute"
            )
            save_blob(
                directory, firmware_name, firmware_cpu, firmware_blob, manifest
            )
            command["firmware_command_file"] = f"{firmware_name}.bin"
            command["firmware_command_size"] = RUN_COMPUTE_CAPTURE_SIZE
            sku_gpu = u64(firmware_blob, RUN_COMPUTE_SKU_POINTER_OFFSET)
            sku_size = u32(firmware_blob, RUN_COMPUTE_SKU_SIZE_OFFSET)
            if (
                valid_graph_kva(sku_encoder)
                and sku_gpu != 0
                and 0 < sku_size <= 0x10000
            ):
                encoder_blob = devmem.read(sku_encoder, AGX_SKU_ENCODER_SIZE)
                encoder_name = (
                    f"queue-{queue['id']:05d}-command-{counter:08x}-sku-encoder"
                )
                save_blob(
                    directory,
                    encoder_name,
                    sku_encoder,
                    encoder_blob,
                    manifest,
                )
                cpu_buffer = u64(encoder_blob, AGX_SKU_ENCODER_CPU_BUFFER)
                block_stride = u64(encoder_blob, AGX_SKU_ENCODER_BLOCK_STRIDE)
                block_count = u32(encoder_blob, AGX_SKU_ENCODER_BLOCK_COUNT)
                block_offset = u32(encoder_blob, AGX_SKU_ENCODER_BLOCK_OFFSET)
                block_index = u32(encoder_blob, AGX_SKU_ENCODER_BLOCK_INDEX)
                write_cursor = u64(encoder_blob, AGX_SKU_ENCODER_WRITE_CURSOR)
                sku_cpu = cpu_buffer + block_offset
                command.update(
                    {
                        "sku_encoder_file": f"{encoder_name}.bin",
                        "sku_cpu": f"0x{sku_cpu:016x}",
                        "sku_gpu": f"0x{sku_gpu:016x}",
                        "sku_size": sku_size,
                        "sku_block_stride": block_stride,
                        "sku_block_count": block_count,
                        "sku_block_index": block_index,
                        "sku_write_cursor": f"0x{write_cursor:016x}",
                    }
                )
                if (
                    valid_graph_kva(sku_cpu)
                    and block_count != 0
                    and block_index < block_count
                    and block_offset == block_stride * block_index
                    and sku_size <= block_stride
                    and sku_cpu <= write_cursor <= sku_cpu + sku_size
                ):
                    sku_blob = devmem.read(sku_cpu, sku_size)
                    sku_name = (
                        f"queue-{queue['id']:05d}-command-{counter:08x}-sku-stream"
                    )
                    save_blob(
                        directory, sku_name, sku_cpu, sku_blob, manifest
                    )
                    command["sku_stream_file"] = f"{sku_name}.bin"
                    command['sku_identity_valid'] = (u64(sku_blob, 0x14) == companion_gpu + 0x20
                                                     and u32(sku_blob, 0x2c) == u32(firmware_blob, 0x0c))
                    if not command['sku_identity_valid']:
                        command['sku_gap'] = 'mutable SKU encoder now references another command; exclude this stream and derive it from the firmware-global UAT instead'

    manifest["compute_queue_objects"] = {
        "layout_basis": (
            "macOS 25G83 IOGPUWorkQueue command-ring methods, "
            "AGXCommandQueue::processCompute, and exact MetaClass allocation sizes"
        ),
        "compute_tail_expansion_enabled": expect_compute_tail,
        "objects": records,
    }


def render_sku_identity(command: bytes, sku: bytes, address: int, stage: str) -> bool:
    reg, context, opcode = (0x40, 0x44, 5) if stage == 'tiling' else (0x80, 0x5c, 7)
    return (address & 0xffffff0000000000 == 0xfffffc0000000000
            and len(command) >= 0x10 and len(sku) >= context + 4
            and u32(command, 0) == (0 if stage == 'tiling' else 1)
            and u32(command, 0xc) < 64 and u32(sku, 0) == opcode
            and u64(sku, 0x14) == address + reg
            and u32(sku, context) == u32(command, 0xc))


def snapshot_render_queue_objects(devmem, directory, manifest):
    """25G83 processRender + exact MetaClass sizes; never expand unknown types."""
    slide = devmem.get_info().kernel_anchor - MACOS_25G83_UNSLID_BZERO
    if slide < 0 or slide & 0x3fff:
        raise RuntimeError('invalid current-boot KASLR anchor')
    def vtable(blob):
        return ((u64(blob, 0) & 0x7fffffffffff) | 0xffff800000000000) - slide
    records = []
    manifest['render_queue_objects'] = {'objects': records,
        'basis': '25G83 processRender 897aa4c; 3DWorkQueue alloc 8a0ef0c; descriptor alloc 894c918/8a054e0; submitBuffer 894b094/8a02de4; SKU producers 89bf3f8/89cb624',
        'snapshot_start_monotonic_ns': time.monotonic_ns()}
    for q in manifest['namespaces']['command-queues'].get('objects', []):
        qb = (directory/q['file']).read_bytes(); qa = int(q['address'],16)
        count = u32(qb,0x86c)
        if count > 4:
            records.append({'error':'3D queue count exceeds fixed native array', 'count':count}); continue
        for i in range(count):
            wa = u64(qb,0x600+8*i)
            if not valid_graph_kva(wa): continue
            wb = devmem.read(wa,0x240)
            rec = {'address':hex(wa),'queue':hex(qa),'commands':[]}; records.append(rec)
            prefix = f"render-queue-{q['id']:05d}-{i}"
            save_blob(directory,prefix,wa,wb,manifest)
            # The workqueue class is checked before following its ring.
            rec['vtable_unslid'] = hex(vtable(wb))
            if vtable(wb) != 0xfffffe0007eaa8b0:
                rec['error']='unexpected workqueue vtable'; continue
            head,tail,end,cap = [u32(wb,o) for o in (0x48,0x4c,0x50,0x54)]
            ring = u64(wb,0x58); n = (end-head)&0xffffffff; submitted=(tail-head)&0xffffffff
            rec['ring']={'head':head,'tail':tail,'end':end,'capacity':cap,'address':hex(ring)}
            if not (0 < cap <= 4096 and submitted <= n <= cap and valid_graph_kva(ring)): continue
            rb = devmem.read(ring,cap*8); save_blob(directory,prefix+'-ring',ring,rb,manifest)
            for j in range(n):
                counter=(head+j)&0xffffffff; da=u64(rb,(counter%cap)*8)
                if not valid_graph_kva(da): continue
                db=devmem.read(da,0x130)
                cr={'address':hex(da),'vtable_unslid':hex(vtable(db)),'submitted':j<submitted}
                rec['commands'].append(cr)
                base=prefix+f'-command-{counter:08x}'
                save_blob(directory,base+'-header',da,db,manifest)
                if u64(db,0x50)!=wa or u64(db,0x60)!=qa:
                    cr['error']='descriptor backlink mismatch'; continue
                kind={0xfffffe0007ea8438:('fragment',0xb70),0xfffffe0007ea8118:('tiling',0x1428)}.get(vtable(db))
                if not kind: cr['gap']='unknown descriptor class, header only'; continue
                cr['kind']=kind[0]; db=devmem.read(da,kind[1]);save_blob(directory,base+'-descriptor',da,db,manifest)
                # TA inherits 3D descriptor, but its own command/encoder are distinct.
                for label,off,size,encoff,skupoff in ([('tiling',0xe90,0x940,0x1420,0x770),('fragment',0x400,0xca0,0x138,0x14)] if kind[0]=='tiling' else [('fragment',0x400,0xca0,0x138,0x14)]):
                    cr = {'stage': label}; rec['commands'][-1].setdefault('firmware_objects', []).append(cr)
                    fa,fg,fc=u64(db,off),u64(db,off+8),u64(db,off+16)
                    cr.update(firmware_cpu=hex(fa),firmware_gpu=hex(fg),firmware_canonical=hex(fc))
                    if not valid_graph_kva(fa) or not fg: continue
                    fb=devmem.read(fa,size);save_blob(directory,base+'-run-'+label,fa,fb,manifest)
                    cr.update(firmware_file=base+'-run-'+label+'.bin', context_id=u32(fb,0xc),
                        command_identity_valid=(u32(fb,0)==(0 if label=='tiling' else 1) and u32(fb,0xc)<64))
                    if not cr['command_identity_valid']:
                        cr['error']='unexpected RunCommand type/context'; continue
                    gpu=u64(fb,skupoff); length=u32(fb,skupoff+8)
                    # Authoritative sequence address belongs to RunCommand,
                    # independently of the mutable producer encoder cursor.
                    if gpu & 0xffffff0000000000 == 0xfffffc0000000000 and 0 < length <= 0x10000:
                        try:
                            parts=resolve_firmware_read_fragments(devmem,gpu,length)
                            sku=read_physical_fragments(devmem,parts)
                            save_blob(directory,base+'-'+label+'-sku-uat',gpu,sku,manifest)
                            manifest['blobs'][base+'-'+label+'-sku-uat']['address_space']='firmware-global'
                            cr['sku_uat_identity_valid']=render_sku_identity(fb,sku,fc,label)
                            cr['sku_uat_fragments']=parts
                            cr['sku_uat_file']=base+'-'+label+'-sku-uat.bin'
                            for part in parts:
                                part['uat_table_files']=save_uat_walk_tables(devmem,directory,manifest,part['uat_walk'])
                        except RuntimeError as error:
                            cr['sku_uat_error']=str(error)
                    ea=u64(db,encoff)
                    if not valid_graph_kva(ea): continue
                    eb=devmem.read(ea,0x80);save_blob(directory,base+'-'+label+'-sku-encoder',ea,eb,manifest)
                    gpu=u64(fb,skupoff); length=u32(fb,skupoff+8)
                    cpu=u64(eb,0x28)+u32(eb,0x5c)
                    # Use the collector's established fields for layout checks.
                    stride=u64(eb,AGX_SKU_ENCODER_BLOCK_STRIDE)
                    blocks=u32(eb,AGX_SKU_ENCODER_BLOCK_COUNT);index=u32(eb,AGX_SKU_ENCODER_BLOCK_INDEX)
                    cursor=u64(eb,AGX_SKU_ENCODER_WRITE_CURSOR)
                    cr.update(sku_gpu=hex(gpu),sku_cpu=hex(cpu),sku_bytes=length)
                    if (valid_graph_kva(cpu) and 0 < length <= stride <= 0x10000 and index < blocks
                        and u32(eb,0x5c)==stride*index and cpu <= cursor <= cpu+length):
                        sku=devmem.read(cpu,length)
                        save_blob(directory,base+'-'+label+'-sku',cpu,sku,manifest)
                        cr['sku_identity_valid']=render_sku_identity(fb,sku,fc,label)
                        if not cr['sku_identity_valid']:
                            cr['sku_gap']='mutable encoder references another command; do not use producer SKU as submission evidence'
    manifest['render_queue_objects']['snapshot_end_monotonic_ns']=time.monotonic_ns()


def capture_compute_completion_records(devmem, directory, manifest, source):
    """Read only pointers established by validated native RunCompute records.

    At complete these are retained addresses, not a claim that the allocator
    still owns the same command. Compare UUID/stamp and preserve changed bytes.
    No MMIO or writes; every firmware read retains its current UAT walk.
    """
    records = manifest.setdefault("compute_completion_records", [])
    for index, command in enumerate(source.get("run_compute_candidates", [])):
        record = {"command": command["address"], "job_meta": command["job_meta"],
                  "stage": manifest.get("stage"), "objects": []}
        records.append(record)
        fields = [("notifier", command["notifier"], 0x200),
                  ("user-stamp", command["job_meta"]["stamp_address"], 4),
                  ("firmware-stamp", command["job_meta"]["firmware_stamp_address"], 4),
                  ("sku", command["sku_address"], command["sku_size"])]
        for role, address, size in fields:
            item = {"role": role, "gpu_va": address, "size": size, "walks": []}
            record["objects"].append(item)
            try:
                cursor = int(address, 16)
                if cursor & 0xffffff0000000000 != 0xfffffc0000000000 or not 0 < size <= 0x1000:
                    raise RuntimeError("not a bounded canonical firmware object")
                payload = bytearray()
                while len(payload) < size:
                    walk = walk_firmware_uat_physical(devmem, cursor)
                    physical = int(walk["physical_address"], 16)
                    length = min(size-len(payload), 0x4000-(physical & 0x3fff))
                    payload.extend(devmem.read_physical(physical, length))
                    item["walks"].append(walk)
                    save_uat_walk_tables(devmem, directory, manifest, walk)
                    cursor += length
                filename = f"completion-{index}-{role}.bin"
                (directory/filename).write_bytes(payload)
                item["file"] = filename
            except RuntimeError as error:
                item["error"] = str(error)


def identify_run_compute_objects(directory: Path, manifest: dict) -> None:
    """Identify same-build G16 RunCompute objects in captured GPU resources."""

    signature = struct.pack("<I", RUN_COMPUTE_REGISTER_WORDS[0])
    candidates = []
    sources = []
    for resource in manifest["namespaces"]["resources"].get("objects", []):
        filename = resource.get("payload_file")
        mappings = resource.get("mappings", [])
        if filename is not None and mappings:
            sources.append((filename, int(mappings[0]["gpu_va"], 16), "gpu-va"))
    for record in manifest.get("compute_queue_objects", {}).get("objects", []):
        ring = record.get("command_ring", {})
        for command in ring.get("commands", []):
            filename = command.get("firmware_command_file")
            gpu_address = int(command.get("firmware_command_gpu", "0"), 16)
            if filename is not None and gpu_address != 0:
                sources.append((filename, gpu_address, "gpu-va"))
    seen = set()
    for filename, base_address, address_space in sources:
        blob = (directory / filename).read_bytes()
        search_offset = 0
        while (register_offset := blob.find(signature, search_offset)) >= 0:
            search_offset = register_offset + 1
            # generateRegisterList() may prepend one 12-byte cache-invalidate
            # register before the otherwise first 0x1a510 entry.
            for prefix_entries in (0, 1):
                work_offset = (
                    register_offset
                    - RUN_COMPUTE_REGISTER_LIST_OFFSET
                    - prefix_entries * 12
                )
                key = (filename, work_offset)
                if key in seen or work_offset < 0:
                    continue
                if work_offset + RUN_COMPUTE_CONTEXT_GENERATION_OFFSET >= len(blob):
                    continue
                if u32(blob, work_offset) != 3:
                    continue
                count_and_size = u32(
                    blob, work_offset + RUN_COMPUTE_REGISTERS_HEADER_OFFSET + 8
                )
                register_count = count_and_size & 0xFFFF
                register_bytes = count_and_size >> 16
                list_offset = work_offset + RUN_COMPUTE_REGISTER_LIST_OFFSET
                list_end = list_offset + register_bytes
                if (
                    register_count < 22
                    or register_count > 0x95
                    or register_bytes != register_count * 12
                    or list_end > work_offset + RUN_COMPUTE_REGISTERS_HEADER_OFFSET
                    or list_end > len(blob)
                ):
                    continue
                register_words = tuple(
                    u32(blob, list_offset + index * 12)
                    for index in range(register_count)
                )
                index = 0
                has_preamble = (
                    register_words[index] == RUN_COMPUTE_OPTIONAL_PREAMBLE_WORD
                )
                if has_preamble:
                    index += 1
                required_head = RUN_COMPUTE_REGISTER_WORDS[:7]
                if register_words[index : index + len(required_head)] != required_head:
                    continue
                index += len(required_head)
                has_krce = register_words[index] == RUN_COMPUTE_OPTIONAL_KRCE_WORD
                if has_krce:
                    index += 1
                required_middle = RUN_COMPUTE_REGISTER_WORDS[7:19]
                if register_words[index : index + len(required_middle)] != required_middle:
                    continue
                index += len(required_middle)
                has_cluster_pair = (
                    register_words[index : index + 2]
                    == RUN_COMPUTE_REGISTER_WORDS[19:21]
                )
                if has_cluster_pair:
                    index += 2
                required_tail = RUN_COMPUTE_REGISTER_WORDS[21:]
                if register_words[index : index + len(required_tail)] != required_tail:
                    continue
                index += len(required_tail)
                register_list_address = u64(
                    blob, work_offset + RUN_COMPUTE_REGISTERS_HEADER_OFFSET
                )
                expected_list_address = (
                    base_address + work_offset + RUN_COMPUTE_REGISTER_LIST_OFFSET
                )
                if register_list_address != expected_list_address:
                    continue
                registers = []
                for entry_index, register_word in enumerate(register_words):
                    entry = list_offset + entry_index * 12
                    registers.append(
                        {
                            "register": f"0x{register_word & 0x3fff8:05x}",
                            "flag": register_word & 1,
                            "value": f"0x{u64(blob, entry + 4):016x}",
                        }
                    )
                # The register array has room for 149 entries, but only the
                # advertised count is consumed. Same-build macOS command
                # buffers retain nonzero allocator contents after that count;
                # report those bytes without misclassifying them as registers.
                unadvertised_register_slots = []
                register_capacity = (
                    RUN_COMPUTE_REGISTERS_HEADER_OFFSET
                    - RUN_COMPUTE_REGISTER_LIST_OFFSET
                ) // 12
                for entry_index in range(register_count, register_capacity):
                    entry = list_offset + entry_index * 12
                    raw = blob[entry : entry + 12]
                    if any(raw):
                        unadvertised_register_slots.append(
                            {
                                "slot": entry_index,
                                "raw": raw.hex(),
                            }
                        )
                seen.add(key)
                candidates.append(
                    {
                        "address": f"0x{base_address + work_offset:016x}",
                        "address_space": address_space,
                        "file": filename,
                        "file_offset": work_offset,
                        "command_type": u32(blob, work_offset),
                        "command_counter": f"0x{u64(blob, work_offset + 4):016x}",
                        "uat_context_id": u32(blob, work_offset + 0xC),
                        "notifier": f"0x{u64(blob, work_offset + 0x10):016x}",
                        "register_list_address": f"0x{register_list_address:016x}",
                        "register_count": register_count,
                        "register_bytes": register_bytes,
                        "has_cache_invalidate_preamble": has_preamble,
                        "has_krce_buffer_register": has_krce,
                        "has_cluster_config_pair": has_cluster_pair,
                        "trailing_register_count": register_count - index,
                        "sku_address": f"0x{u64(blob, work_offset + RUN_COMPUTE_SKU_POINTER_OFFSET):016x}",
                        "sku_size": u32(blob, work_offset + RUN_COMPUTE_SKU_SIZE_OFFSET),
                        "job_params2": {
                            "preemption_buffer": f"0x{u64(blob, work_offset + RUN_COMPUTE_JOB_PARAMS2_PREEMPT_BUFFER_OFFSET):016x}",
                            "control_stream_end": f"0x{u64(blob, work_offset + RUN_COMPUTE_JOB_PARAMS2_CONTROL_STREAM_END_OFFSET):016x}",
                            "g16_config": f"0x{u64(blob, work_offset + RUN_COMPUTE_JOB_PARAMS2_G16_CONFIG_OFFSET):016x}",
                        },
                        "encoder_parameters": {
                            "global_trace_id": u32(blob, work_offset + RUN_COMPUTE_ENCODER_GLOBAL_TRACE_ID_OFFSET),
                            "unknown_mask": f"0x{u32(blob, work_offset + RUN_COMPUTE_ENCODER_UNKNOWN_MASK_OFFSET):08x}",
                        },
                        "job_meta": {
                            "stamp_address": f"0x{u64(blob, work_offset + RUN_COMPUTE_JOB_STAMP_ADDRESS_OFFSET):016x}",
                            "firmware_stamp_address": f"0x{u64(blob, work_offset + RUN_COMPUTE_JOB_FIRMWARE_STAMP_ADDRESS_OFFSET):016x}",
                            "stamp_value": u32(blob, work_offset + RUN_COMPUTE_JOB_STAMP_VALUE_OFFSET),
                            "stamp_slot": u32(blob, work_offset + RUN_COMPUTE_JOB_STAMP_SLOT_OFFSET),
                            "event_control_index": u32(blob, work_offset + RUN_COMPUTE_JOB_EVENT_CONTROL_INDEX_OFFSET),
                            "flush_stamps": u32(blob, work_offset + RUN_COMPUTE_JOB_FLUSH_STAMPS_OFFSET),
                            "uuid": u32(blob, work_offset + RUN_COMPUTE_JOB_UUID_OFFSET),
                            "event_sequence": u32(blob, work_offset + RUN_COMPUTE_JOB_EVENT_SEQUENCE_OFFSET),
                        },
                        "timestamp_pointers": {
                            "start": f"0x{u64(blob, work_offset + RUN_COMPUTE_TIMESTAMP_START_OFFSET):016x}",
                            "end": f"0x{u64(blob, work_offset + RUN_COMPUTE_TIMESTAMP_END_OFFSET):016x}",
                        },
                        "uma_address": f"0x{u64(blob, work_offset + RUN_COMPUTE_UMA_POINTER_OFFSET):016x}",
                        "uma_flush": blob[
                            work_offset + RUN_COMPUTE_UMA_FLUSH_OFFSET
                        ]
                        & 1,
                        "context_store_request": f"0x{u64(blob, work_offset + RUN_COMPUTE_CONTEXT_STORE_REQUEST_OFFSET):016x}",
                        "context_store_complete": f"0x{u64(blob, work_offset + RUN_COMPUTE_CONTEXT_STORE_COMPLETE_OFFSET):016x}",
                        "engine_command_address": f"0x{u64(blob, work_offset + RUN_COMPUTE_ENGINE_COMMAND_ADDRESS_OFFSET):016x}",
                        "context_generation": blob[
                            work_offset + RUN_COMPUTE_CONTEXT_GENERATION_OFFSET
                        ],
                        "registers": registers,
                        "unadvertised_register_slots": unadvertised_register_slots,
                    }
                )
    manifest["run_compute_candidates"] = candidates


def snapshot_resource(
    devmem: DevMem,
    directory: Path,
    resource_id: int,
    resource_address: int,
    resource: bytes,
    namespace_info: dict,
    manifest: dict,
) -> None:
    record = namespace_info["objects"][-1]
    memory = u64(resource, RESOURCE_MEMORY)
    record.update(
        {
            "type": resource[RESOURCE_TYPE_FLAGS],
            "flags": f"0x{u32(resource, RESOURCE_TYPE_FLAGS):08x}",
            "client_shared": f"0x{u64(resource, RESOURCE_CLIENT_SHARED):016x}",
            "memory": f"0x{memory:016x}",
            "i_gpu": f"0x{u64(resource, RESOURCE_IOGPU):016x}",
            "shared_machine": f"0x{u64(resource, RESOURCE_SHARED_MACHINE):016x}",
            "mappings": [],
        }
    )
    if not valid_kva(memory):
        return
    memory_blob = devmem.read(memory, 0xA0)
    save_blob(
        directory,
        f"resource-{resource_id:05d}-memory",
        memory,
        memory_blob,
        manifest,
    )
    descriptor = u64(memory_blob, SYSMEM_DESCRIPTOR)
    record.update(
        {
            "client_address": f"0x{u64(memory_blob, SYSMEM_CLIENT_ADDRESS):016x}",
            "descriptor": f"0x{descriptor:016x}",
            "mapping_mode": u32(memory_blob, MEMORY_MAPPING_MODE),
            "mapping_count": u32(memory_blob, MEMORY_MAPPING_COUNT),
        }
    )
    if valid_kva(descriptor):
        save_blob(
            directory,
            f"resource-{resource_id:05d}-descriptor",
            descriptor,
            devmem.read(descriptor, 0x100),
            manifest,
        )
    count = record["mapping_count"]
    if count == 0 or count > 256:
        return
    if record["mapping_mode"] == 2:
        mapping_array = memory + MEMORY_INLINE_MAPPINGS
    else:
        mapping_array = u64(memory_blob, MEMORY_MAPPING_ARRAY)
    if not valid_kva(mapping_array):
        return
    mapping_pointers = devmem.read(mapping_array, count * 8)
    save_blob(
        directory,
        f"resource-{resource_id:05d}-mapping-pointers",
        mapping_array,
        mapping_pointers,
        manifest,
    )
    for index in range(count):
        mapping = u64(mapping_pointers, index * 8)
        if not valid_kva(mapping):
            continue
        mapping_blob = devmem.read(mapping, 0x80)
        save_blob(
            directory,
            f"resource-{resource_id:05d}-mapping-{index:03d}",
            mapping,
            mapping_blob,
            manifest,
        )
        record["mappings"].append(
            {
                "index": index,
                "address": f"0x{mapping:016x}",
                "memory": f"0x{u64(mapping_blob, MEMORY_MAP_MEMORY):016x}",
                "gpu_task": f"0x{u64(mapping_blob, MEMORY_MAP_GPU_TASK):016x}",
                "gpu_va": f"0x{u64(mapping_blob, MEMORY_MAP_GPU_VA):016x}",
            }
        )


def dump_resource_payload(
    devmem: DevMem, directory: Path, resource_id: int, manifest: dict
) -> None:
    records = manifest["namespaces"]["resources"]["objects"]
    matches = [record for record in records if record["id"] == resource_id]
    if len(matches) != 1:
        raise RuntimeError(f"resource {resource_id}: expected one manifest record")
    record = matches[0]
    descriptor = int(record.get("descriptor", "0"), 16)
    if not valid_kva(descriptor):
        record["payload_error"] = "invalid descriptor pointer"
        return
    try:
        mapping = devmem.retain_memory_descriptor(descriptor)
        record["mapped_address"] = f"0x{mapping.mapped_address:016x}"
        record["mapped_length"] = mapping.mapped_length
        if mapping.mapped_length > MAX_PAYLOAD_LENGTH:
            record["payload_error"] = (
                f"mapped length exceeds 0x{MAX_PAYLOAD_LENGTH:x}-byte capture cap"
            )
            return
        payload = devmem.read_memory_descriptor(0, mapping.mapped_length)
        name = f"resource-{resource_id:05d}-payload"
        save_blob(directory, name, mapping.mapped_address, payload, manifest)
        record["payload_file"] = f"{name}.bin"
    except RuntimeError as error:
        record["payload_error"] = str(error)


def write_manifest(directory: Path, manifest: dict) -> None:
    (directory / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")


def snapshot(
    devmem: DevMem,
    entry_id: int,
    directory: Path,
    stage: str,
    capture_physical: bool = False,
) -> dict:
    stage_dir = directory / stage
    stage_dir.mkdir(parents=True, exist_ok=False)
    user_client = devmem.retain_registry_entry(entry_id)
    uc_blob = devmem.read(user_client, 0x180)
    device = u64(uc_blob, UC_IOGPU_DEVICE)
    if not valid_kva(device):
        raise RuntimeError(f"invalid IOGPUDevice pointer 0x{device:016x}")
    device_blob = devmem.read(device, 0x200)
    pid = u32(device_blob, DEVICE_PID)
    manifest = {
        "layout": "macOS-25G83-IOGPU",
        "stage": stage,
        "registry_entry_id": f"0x{entry_id:016x}",
        "user_client": f"0x{user_client:016x}",
        "device": f"0x{device:016x}",
        "pid": pid,
        "i_gpu": f"0x{u64(device_blob, DEVICE_IOGPU):016x}",
        "task": f"0x{u64(device_blob, DEVICE_TASK):016x}",
        "gpu_task": f"0x{u64(device_blob, DEVICE_GPU_TASK):016x}",
        "namespaces": {},
        "blobs": {},
    }
    save_blob(stage_dir, "agx-device-user-client", user_client, uc_blob, manifest)
    save_blob(stage_dir, "io-gpu-device", device, device_blob, manifest)
    gpu_task = u64(device_blob, DEVICE_GPU_TASK)
    if valid_kva(gpu_task):
        save_blob(stage_dir, "io-gpu-task", gpu_task, devmem.read(gpu_task, 0x200), manifest)
    snapshot_namespace(
        devmem,
        stage_dir,
        "resources",
        u64(device_blob, DEVICE_RESOURCE_NAMESPACE),
        manifest,
    )
    snapshot_namespace(
        devmem,
        stage_dir,
        "command-queues",
        u64(device_blob, DEVICE_COMMAND_QUEUE_NAMESPACE),
        manifest,
    )
    if capture_physical:
        snapshot_physical_gpu_state(devmem, stage_dir, manifest)
    write_manifest(stage_dir, manifest)
    return manifest


def snapshot_live_compute(
    devmem: DevMem,
    directory: Path,
    submitted: dict,
    expect_compute_tail: bool,
    capture_physical: bool = False,
    capture_usc_private_memory: bool = False,
    capture_mmio: bool = False,
) -> dict:
    """Capture only the proven live queue path while the shader gate is held."""

    stage_dir = directory / "executing"
    stage_dir.mkdir(parents=True, exist_ok=False)
    command_queues = []
    manifest = {
        "layout": submitted["layout"],
        "stage": "executing",
        "registry_entry_id": submitted["registry_entry_id"],
        "user_client": submitted["user_client"],
        "device": submitted["device"],
        "pid": submitted["pid"],
        "i_gpu": submitted["i_gpu"],
        "task": submitted["task"],
        "gpu_task": submitted["gpu_task"],
        "namespaces": {
            "resources": {"objects": []},
            "command-queues": {"objects": command_queues},
        },
        "blobs": {},
        "capture_scope": (
            "live exact queue -> AGXCLWorkQueue -> IOGPU command ring; "
            "broad resource payload capture intentionally deferred until gate release"
        ),
    }
    for queue in submitted["namespaces"]["command-queues"].get("objects", []):
        address = int(queue["address"], 16)
        blob = devmem.read(address, 0x1000)
        name = f"command-queues-{queue['id']:05d}"
        save_blob(stage_dir, name, address, blob, manifest)
        command_queues.append({**queue, "file": f"{name}.bin"})
    snapshot_render_queue_objects(devmem, stage_dir, manifest)
    snapshot_compute_queue_objects(
        devmem,
        stage_dir,
        manifest,
        expect_compute_tail=expect_compute_tail,
        include_inactive=True,
    )
    identify_run_compute_objects(stage_dir, manifest)
    capture_compute_completion_records(devmem, stage_dir, manifest, manifest)
    if capture_physical:
        snapshot_physical_gpu_state(
            devmem,
            stage_dir,
            manifest,
            resource_manifest=submitted,
            capture_usc_private_memory=capture_usc_private_memory,
            capture_mmio=capture_mmio,
        )
    return manifest


def changed_ranges(before: bytes, after: bytes) -> list[dict]:
    ranges = []
    start = None
    limit = max(len(before), len(after))
    for offset in range(limit):
        old = before[offset] if offset < len(before) else None
        new = after[offset] if offset < len(after) else None
        if old != new and start is None:
            start = offset
        elif old == new and start is not None:
            ranges.append({"offset": start, "length": offset - start})
            start = None
    if start is not None:
        ranges.append({"offset": start, "length": limit - start})
    return ranges


def write_diff(directory: Path, before_stage: str, after_stage: str) -> None:
    before_dir = directory / before_stage
    after_dir = directory / after_stage
    report = {"before": before_stage, "after": after_stage, "changed": {}}
    for before in sorted(before_dir.glob("*.bin")):
        after = after_dir / before.name
        if not after.exists():
            continue
        ranges = changed_ranges(before.read_bytes(), after.read_bytes())
        if ranges:
            report["changed"][before.name] = ranges
    (directory / f"diff-{before_stage}-to-{after_stage}.json").write_text(
        json.dumps(report, indent=2) + "\n"
    )


def wait_path(path: Path, process: subprocess.Popen, timeout: float = 30.0,
              poll_interval: float = 0.05) -> None:
    deadline = time.monotonic() + timeout
    while not path.exists():
        status = process.poll()
        if status is not None:
            raise RuntimeError(f"metalprobe exited early with status {status}")
        if time.monotonic() >= deadline:
            raise RuntimeError(f"timed out waiting for {path}")
        time.sleep(poll_interval)


def wait_compute_work_queue_ready(
    devmem: DevMem, manifest: dict, timeout: float = 10.0
) -> None:
    """Wait until a proven compute workqueue has a submitted command."""

    deadline = time.monotonic() + timeout
    queues = manifest["namespaces"]["command-queues"].get("objects", [])
    while True:
        for queue in queues:
            address = int(queue["address"], 16)
            blob = devmem.read(address, 0x1000)
            active = u64(blob, QUEUE_ACTIVE_CL_WORK_QUEUE)
            count = u32(blob, QUEUE_CL_WORK_QUEUE_COUNT)
            capacity = u32(blob, QUEUE_CL_WORK_QUEUE_CAPACITY)
            protection = u64(blob, QUEUE_PROTECTION_OPTIONS)
            protected_index = {1: 0, 3: 1, 7: 2}.get(protection)
            protected = (
                u64(blob, QUEUE_PROTECTED_CL_WORK_QUEUES + protected_index * 8)
                if protected_index is not None
                else 0
            )
            ordinary = (
                u64(blob, QUEUE_CL_WORK_QUEUES)
                if 0 < count <= capacity <= 64
                else 0
            )
            for pointer in (active, protected, ordinary):
                if not valid_graph_kva(pointer):
                    continue
                work_queue = devmem.read(pointer, AGX_CL_WORK_QUEUE_SIZE)
                completed = u32(work_queue, IOGPU_WORK_QUEUE_COMPLETED_HEAD)
                submitted = u32(work_queue, IOGPU_WORK_QUEUE_SUBMITTED_TAIL)
                ring_capacity = u32(work_queue, IOGPU_WORK_QUEUE_RING_CAPACITY)
                if 0 < ring_capacity <= 0x10000 and submitted > completed:
                    return
        if time.monotonic() >= deadline:
            raise RuntimeError(
                "timed out waiting for an outstanding submitted AGX compute command"
            )
        time.sleep(0.001)


def select_compute_scheduler_index(manifest: dict) -> int:
    """Return the scheduler index owning this single-queue probe process."""

    compute_queue_ids = {
        queue["queue_id"]
        for queue in manifest.get("compute_queue_objects", {}).get("objects", [])
        if queue.get("valid")
    }
    command_queues = manifest["namespaces"]["command-queues"].get(
        "objects", []
    )
    # With --gate-submit, AGX deliberately does not create the lazy CL
    # workqueue until the shared event is released. The controlled probe owns
    # exactly one IOGPU command queue, so its statically captured scheduler
    # index is the unambiguous pre-release identity. Once execution begins,
    # the live snapshot validates the CL channel/workqueue backlinks.
    if not compute_queue_ids and len(command_queues) == 1:
        compute_queue_ids = {command_queues[0]["id"]}
    indices = {
        int(queue["scheduler_index"])
        for queue in command_queues
        if queue.get("id") in compute_queue_ids
        and queue.get("scheduler_index") is not None
    }
    if len(indices) != 1:
        raise RuntimeError(
            "launch timeline requires exactly one valid compute scheduler "
            f"index, found {sorted(indices)}"
        )
    index = indices.pop()
    if not 0 <= index < J713_FW_SCHED_QUEUE_ENTRY_COUNT:
        raise RuntimeError(f"firmware scheduler index {index} is out of range")
    return index


def resolve_firmware_read_fragments(
    devmem: DevMem, gpu_va: int, length: int
) -> list[dict]:
    """Resolve a bounded firmware-global range once for repeated physical reads."""

    fragments = []
    cursor = gpu_va
    remaining = length
    while remaining:
        walk = walk_firmware_uat_physical(devmem, cursor)
        physical = int(walk["physical_address"], 16)
        chunk = min(remaining, 0x4000 - (physical & J713_UAT_PAGE_MASK))
        fragments.append(
            {
                "gpu_va": f"0x{cursor:016x}",
                "physical_address": f"0x{physical:016x}",
                "length": chunk,
                "uat_walk": walk,
            }
        )
        cursor += chunk
        remaining -= chunk
    return fragments


def read_physical_fragments(devmem: DevMem, fragments: list[dict]) -> bytes:
    return b"".join(
        devmem.read_physical(
            int(fragment["physical_address"], 16), fragment["length"]
        )
        for fragment in fragments
    )


def decode_launch_timeline_record(record: bytes) -> dict:
    if len(record) != J713_LAUNCH_TIMELINE_RECORD_SIZE:
        raise RuntimeError(
            f"launch timeline record is {len(record)} bytes, expected "
            f"{J713_LAUNCH_TIMELINE_RECORD_SIZE}"
        )
    (
        start_ns,
        end_ns,
        power_state,
        flags,
        issue,
        interrupts,
        hardware_queue,
        engine,
        queue,
    ) = struct.unpack(J713_LAUNCH_TIMELINE_RECORD_FORMAT, record)
    issue_words = [u64(issue, offset) for offset in range(0, len(issue), 8)]
    engine_words = [u64(engine, offset) for offset in range(0, len(engine), 8)]
    return {
        "sample_start_ns": start_ns,
        "sample_end_ns": end_ns,
        "sample_skew_ns": end_ns - start_ns,
        "gpu_power_state": power_state,
        "flags": f"0x{flags:08x}",
        "sgx_mmio_valid": bool(flags & J713_LAUNCH_TIMELINE_MMIO_VALID),
        "engine_issue": {
            "dependency_slot_mask": f"0x{issue_words[0]:016x}",
            "register_list_base_and_count": f"0x{issue_words[1]:016x}",
            "issue_control": f"0x{issue_words[2]:016x}",
            "selected_slot_mask": f"0x{issue_words[3]:016x}",
        },
        "interrupt_status_words": [
            f"0x{u64(interrupts, offset):016x}"
            for offset in range(0, len(interrupts), 8)
        ],
        "hardware_queue_status": f"0x{u64(hardware_queue, 0):016x}",
        "scheduler_engine_state": {
            "free_engine_slot_mask": f"0x{engine_words[0]:016x}",
            "words": [f"0x{word:016x}" for word in engine_words],
        },
        "scheduler_queue_entry": {
            "retired_cursor": u32(queue, 0x00),
            "unknown_sequence": u32(queue, 0x04),
            "published_cursor": queue[0x08],
            "started_cursor": queue[0x09],
            "hardware_enqueued_cursor": queue[0x0A],
            "node_ring": f"0x{u64(queue, 0x10):016x}",
            "queue_info": f"0x{u64(queue, 0x18):016x}",
            "hardware_work_class": u32(queue, 0x20),
            "scheduling_class": queue[0x24],
        },
    }


def capture_launch_timeline(
    devmem: DevMem,
    directory: Path,
    manifest: dict,
    execute_path: Path,
    process: subprocess.Popen,
    duration_ms: int,
    capture_mmio: bool = False,
    defer_submit_release_until_powered: bool = False,
) -> dict:
    """Sample the successful pre-slot launch boundary around gate release."""

    scheduler_index = select_compute_scheduler_index(manifest)
    engine_fragments = resolve_firmware_read_fragments(
        devmem,
        J713_FW_SCHED_ENGINE_STATE_VA,
        J713_FW_SCHED_ENGINE_STATE_SIZE,
    )
    queue_gpu_va = (
        J713_FW_SCHED_QUEUE_TABLE_VA
        + scheduler_index * J713_FW_SCHED_QUEUE_ENTRY_SIZE
    )
    queue_fragments = resolve_firmware_read_fragments(
        devmem, queue_gpu_va, J713_FW_SCHED_QUEUE_ENTRY_SIZE
    )

    runtime_pointer_fragments = resolve_firmware_read_fragments(
        devmem, J713_FW_SCHED_RUNTIME_POINTER_VA, 8
    )
    runtime = u64(read_physical_fragments(devmem, runtime_pointer_fragments), 0)
    if runtime & 0xFFFFFF0000000000 != 0xFFFFFC0000000000:
        raise RuntimeError(
            f"invalid firmware scheduler runtime pointer 0x{runtime:016x}"
        )
    power_pointer_fragments = resolve_firmware_read_fragments(
        devmem, runtime + J713_RUNTIME_POWER_PERFORMANCE_OFFSET, 8
    )
    power_performance = u64(
        read_physical_fragments(devmem, power_pointer_fragments), 0
    )
    if power_performance & 0xFFFFFF0000000000 != 0xFFFFFC0000000000:
        raise RuntimeError(
            "invalid firmware GPU power-performance pointer "
            f"0x{power_performance:016x}"
        )
    power_state_gpu_va = (
        power_performance + J713_PWRPERF_GPU_POWER_STATE_OFFSET
    )
    power_state_fragments = resolve_firmware_read_fragments(
        devmem, power_state_gpu_va, 4
    )

    sampling_started = False
    consecutive_powered_samples = 0

    def sample() -> bytes:
        nonlocal consecutive_powered_samples
        start_ns = time.monotonic_ns()
        power_state = u32(
            read_physical_fragments(devmem, power_state_fragments), 0
        )
        if (
            sampling_started
            and power_state != J713_GPU_PERFORMANCE_STATE_OFF
        ):
            consecutive_powered_samples += 1
        else:
            consecutive_powered_samples = 0
        flags = 0
        issue = bytes(J713_SGX_ENGINE_ISSUE_SIZE)
        interrupts = bytes(J713_SGX_INTERRUPT_STATUS_SIZE)
        hardware_queue = bytes(J713_SGX_HARDWARE_QUEUE_STATUS_SIZE)
        # Direct reads from the SGX aperture cause a fatal LLC slave error
        # while its power domain is off. Firmware RAM is always readable, so
        # require two consecutive post-trigger nonzero performance-state
        # samples. Firmware's agx_initialize_after_power_on() uses this exact
        # field as its powered-on predicate and fatals when it remains zero.
        if capture_mmio and consecutive_powered_samples >= 2:
            flags |= J713_LAUNCH_TIMELINE_MMIO_VALID
            issue = devmem.read_mmio(
                J713_SGX_MAIN_BASE + J713_SGX_ENGINE_ISSUE_OFFSET,
                J713_SGX_ENGINE_ISSUE_SIZE,
            )
            interrupts = devmem.read_mmio(
                J713_SGX_MAIN_BASE + J713_SGX_INTERRUPT_STATUS_OFFSET,
                J713_SGX_INTERRUPT_STATUS_SIZE,
            )
            hardware_queue = devmem.read_mmio(
                J713_SGX_MAIN_BASE + J713_SGX_HARDWARE_QUEUE_STATUS_OFFSET,
                J713_SGX_HARDWARE_QUEUE_STATUS_SIZE,
            )
        engine = read_physical_fragments(devmem, engine_fragments)
        queue = read_physical_fragments(devmem, queue_fragments)
        end_ns = time.monotonic_ns()
        return struct.pack(
            J713_LAUNCH_TIMELINE_RECORD_FORMAT,
            start_ns,
            end_ns,
            power_state,
            flags,
            issue,
            interrupts,
            hardware_queue,
            engine,
            queue,
        )

    records = [sample()]
    execute_path.touch()
    sampling_started = True
    trigger_ns = time.monotonic_ns()
    release_ns = None if defer_submit_release_until_powered else trigger_ns
    deadline_ns = trigger_ns + duration_ms * 1_000_000
    executing_observed_ns = None
    while time.monotonic_ns() < deadline_ns:
        if process.poll() is not None:
            raise RuntimeError(
                "metalprobe exited during launch timeline with status "
                f"{process.returncode}"
            )
        records.append(sample())
        if (
            defer_submit_release_until_powered
            and release_ns is None
            and consecutive_powered_samples >= 2
        ):
            (execute_path.parent / "release-submit").touch()
            release_ns = time.monotonic_ns()
        if (
            executing_observed_ns is None
            and (execute_path.parent / "executing").exists()
        ):
            executing_observed_ns = time.monotonic_ns()
    if release_ns is None:
        raise RuntimeError(
            "GPU did not reach two consecutive powered-on performance-state "
            "samples "
            "before the launch-timeline deadline"
        )

    binary_name = "launch-timeline.bin"
    binary = b"".join(records)
    (directory / binary_name).write_bytes(binary)
    manifest["blobs"]["launch-timeline"] = {
        "address_space": "mixed-read-only-launch-state",
        "length": len(binary),
        "file": binary_name,
    }

    changes = []
    previous_payload = None
    for index, record in enumerate(records):
        # Ignore the two timestamps for change detection while retaining them
        # in both the raw record and decoded change point.
        payload = record[16:]
        if index == 0 or payload != previous_payload:
            decoded = decode_launch_timeline_record(record)
            decoded["sample_index"] = index
            decoded["relative_to_release_ns"] = (
                decoded["sample_start_ns"] - release_ns
            )
            changes.append(decoded)
        previous_payload = payload

    table_files = []
    for fragment in (
        engine_fragments
        + queue_fragments
        + runtime_pointer_fragments
        + power_pointer_fragments
        + power_state_fragments
    ):
        for table_file in save_uat_walk_tables(
            devmem, directory, manifest, fragment["uat_walk"]
        ):
            if table_file not in table_files:
                table_files.append(table_file)
    result = {
        "structure": "g16_successful_compute_launch_timeline",
        "capture_boundary": "submission-shared-event-release-to-live-shader-gate",
        "record_format": J713_LAUNCH_TIMELINE_RECORD_FORMAT,
        "record_size": J713_LAUNCH_TIMELINE_RECORD_SIZE,
        "sample_count": len(records),
        "duration_ms": duration_ms,
        "execute_trigger_monotonic_ns": trigger_ns,
        "release_monotonic_ns": release_ns,
        "executing_stage_observed_monotonic_ns": executing_observed_ns,
        "raw_file": binary_name,
        "read_order": [
            "sgx-engine-issue",
            "sgx-interrupt-status",
            "sgx-hardware-queue-status",
            "firmware-scheduler-engine-state",
            "firmware-scheduler-queue-entry",
        ],
        "scheduler_index": scheduler_index,
        "scheduler_queue_entry_gpu_va": f"0x{queue_gpu_va:016x}",
        "gpu_power_state_gpu_va": f"0x{power_state_gpu_va:016x}",
        "sgx_mmio_gate": (
            "two-consecutive-post-trigger-nonzero-gpu-performance-state-samples"
            if capture_mmio
            else "disabled-by-firmware-only-preflight"
        ),
        "submit_release_mode": (
            "collector-releases-after-powered-on-warmup"
            if defer_submit_release_until_powered
            else "commandprobe-releases-after-execute-trigger"
        ),
        "engine_state_fragments": engine_fragments,
        "scheduler_queue_entry_fragments": queue_fragments,
        "runtime_pointer_fragments": runtime_pointer_fragments,
        "power_pointer_fragments": power_pointer_fragments,
        "gpu_power_state_fragments": power_state_fragments,
        "uat_table_files": table_files,
        "change_points": changes,
    }
    (directory / "launch-timeline.json").write_text(
        json.dumps(result, indent=2) + "\n"
    )
    return result


def capture(
    probe: Path,
    output: Path,
    probe_value: int | None,
    probe_args: list[str],
    expand_compute_tail: bool = True,
    capture_physical: bool = False,
    capture_usc_private_memory: bool = False,
    capture_launch_timeline_enabled: bool = False,
    launch_timeline_mmio_enabled: bool = False,
    physical_mmio_enabled: bool = False,
    launch_timeline_ms: int = 250,
    ptov_address: int | None = None,
    expected_output_bytes: int | None = None,
) -> None:
    if output.exists():
        raise RuntimeError(f"refusing to overwrite {output}")
    output.mkdir(parents=True)
    pause = output / "control"
    log_path = output / "metalprobe.log"
    with log_path.open("wb") as log:
        command = [str(probe), "--pause-dir", str(pause)]
        if probe_value is not None:
            command.extend(["--value", f"0x{probe_value:08x}"])
        command.extend(probe_args)
        process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT)
        devmem = None
        try:
            wait_path(pause / "created", process)
            entry = find_user_client(process.pid)
            entry_id = int(entry["IORegistryEntryID"])
            (output / "ioreg-entry.plist").write_bytes(plistlib.dumps(entry))
            devmem = DevMem()
            if capture_physical:
                info = devmem.get_info()
                ptov_resolution = resolve_macos_25g83_ptov(
                    info.kernel_anchor
                )
                resolved_ptov = int(
                    ptov_resolution["physical_translator"], 16
                )
                if (
                    ptov_address is not None
                    and ptov_address != resolved_ptov
                ):
                    raise RuntimeError(
                        "explicit --ptov-address does not match the live "
                        "25G83 kernel anchor: "
                        f"0x{ptov_address:016x} != 0x{resolved_ptov:016x}"
                    )
                devmem.configure_ptov(resolved_ptov)
                (output / "physical-translator.json").write_text(
                    json.dumps(ptov_resolution, indent=2) + "\n"
                )
            created = snapshot(
                devmem,
                entry_id,
                output,
                "created",
                capture_physical=capture_physical,
            )
            if created["pid"] != process.pid:
                raise RuntimeError(
                    f"IOGPUDevice pid {created['pid']} != probe pid {process.pid}"
                )
            created_ids = {
                record["id"]
                for record in created["namespaces"]["resources"]["objects"]
            }
            if not created_ids:
                raise RuntimeError("probe created no IOGPU resources")
            # The probe allocates its output buffer last, immediately before
            # publishing the created stage. Validate that inference by its
            # exact requested descriptor length. The replay-grade ocean probe
            # is the sole large-output case: 2560 * 1440 packed RGBA8 pixels.
            output_id = max(created_ids)
            for resource_id in sorted(created_ids):
                dump_resource_payload(
                    devmem, output / "created", resource_id, created
                )
            output_record = next(
                record
                for record in created["namespaces"]["resources"]["objects"]
                if record["id"] == output_id
            )
            expected_output_length = (
                2560 * 1440 * 4
                if any(
                    probe_args[index : index + 2] == ["--program", "ocean"]
                    for index in range(max(len(probe_args) - 1, 0))
                )
                else 0x4000
            )
            if expected_output_bytes is not None:
                expected_output_length = expected_output_bytes
            if output_record.get("mapped_length") != expected_output_length:
                raise RuntimeError(
                    f"last-created resource {output_id} has length "
                    f"{output_record.get('mapped_length')}, expected "
                    f"{expected_output_length}"
                )
            created["inferences"] = {
                "output_resource_id": output_id,
                "basis": (
                    "last resource created by probe; descriptor length is "
                    f"{expected_output_length}"
                ),
            }
            created["probe_command"] = command
            write_manifest(output / "created", created)
            (pause / "encode").touch()
            wait_path(pause / "encoded", process)
            encoded = snapshot(
                devmem,
                entry_id,
                output,
                "encoded",
                capture_physical=capture_physical,
            )
            encoded_ids = {
                record["id"]
                for record in encoded["namespaces"]["resources"]["objects"]
            }
            new_ids = sorted(encoded_ids - created_ids)
            for resource_id in sorted(encoded_ids):
                dump_resource_payload(
                    devmem, output / "encoded", resource_id, encoded
                )
            encoded["inferences"] = {
                "output_resource_id": output_id,
                "encoding_created_resource_ids": new_ids,
            }
            encoded["probe_command"] = command
            write_manifest(output / "encoded", encoded)
            (pause / "submit").touch()
            wait_path(pause / "submitted", process)
            submitted = snapshot(
                devmem,
                entry_id,
                output,
                "submitted",
                capture_physical=capture_physical,
            )
            workload = None
            if "--workload" in probe_args:
                workload_index = probe_args.index("--workload")
                if workload_index + 1 < len(probe_args):
                    workload = probe_args[workload_index + 1]
            elif "--stage" in probe_args:
                stage_index = probe_args.index("--stage")
                if (
                    stage_index + 1 < len(probe_args)
                    and probe_args[stage_index + 1] == "compute"
                ):
                    # shaderprobe always uses a direct dispatch. Name its
                    # submission shape so the same bounded RunCompute tail
                    # expansion used by commandprobe remains enabled.
                    workload = "compute-direct"
            snapshot_compute_queue_objects(
                devmem,
                output / "submitted",
                submitted,
                expect_compute_tail=(
                    expand_compute_tail
                    and workload
                    in {"compute-direct", "compute-threadgroups", "compute-indirect"}
                ),
            )
            # The event is still closed here: retain render descriptors before
            # short passes retire and leave the process workqueue. Previously
            # these were captured only at the optional executing-stage gate.
            if workload == "render-draw":
                snapshot_render_queue_objects(devmem, output / "submitted", submitted)
            submitted_ids = {
                record["id"]
                for record in submitted["namespaces"]["resources"]["objects"]
            }
            for resource_id in sorted(submitted_ids):
                dump_resource_payload(
                    devmem, output / "submitted", resource_id, submitted
                )
            identify_run_compute_objects(output / "submitted", submitted)
            if capture_physical:
                snapshot_physical_gpu_state(
                    devmem, output / "submitted", submitted
                )
            submitted["inferences"] = {
                **encoded["inferences"],
                "submission_created_resource_ids": sorted(
                    submitted_ids - encoded_ids
                ),
            }
            submitted["probe_command"] = command
            if capture_launch_timeline_enabled:
                submitted["launch_timeline"] = capture_launch_timeline(
                    devmem,
                    output / "submitted",
                    submitted,
                    pause / "execute",
                    process,
                    launch_timeline_ms,
                    capture_mmio=launch_timeline_mmio_enabled,
                    defer_submit_release_until_powered=(
                        "--warmup-before-submit-release" in probe_args
                    ),
                )
            else:
                # Serialize the potentially large resource manifest before the
                # finite draw starts; disk/JSON work can outlast the GPU batch.
                write_manifest(output / "submitted", submitted)
                (pause / "execute").touch()
            if capture_launch_timeline_enabled:
                write_manifest(output / "submitted", submitted)
            gate_shader = (
                "--gate-shader" in probe_args or "--gate-target" in probe_args
                or "--capture-render" in probe_args
            )
            if gate_shader:
                wait_path(pause / "executing", process, poll_interval=0.001)
                if "--capture-render" not in probe_args:
                    wait_compute_work_queue_ready(devmem, submitted)
                executing = snapshot_live_compute(
                    devmem,
                    output,
                    submitted,
                    expect_compute_tail=(
                        expand_compute_tail
                        and workload
                        in {"compute-direct", "compute-threadgroups", "compute-indirect"}
                    ),
                    capture_physical=capture_physical,
                    capture_usc_private_memory=capture_usc_private_memory,
                    capture_mmio=physical_mmio_enabled,
                )
                executing["inferences"] = submitted["inferences"]
                executing["probe_command"] = command
                executing["inferences"]["executing_gate"] = (
                    "ungated-finite-render" if "--capture-render" in probe_args else
                    "target" if "--gate-target" in probe_args else "separate-shader"
                )
                if "--gate-target" in probe_args:
                    sgx_mmio = executing.get("physical_gpu_state", {}).get(
                        "sgx_mmio"
                    )
                    if sgx_mmio is not None:
                        sgx_mmio["capture_boundary"] = (
                            "post-timeline-target-gate-global-sgx-snapshot"
                        )
                        candidates = executing.get("run_compute_candidates", [])
                        issue = sgx_mmio.get("engine_issue") or {}
                        if len(candidates) == 1:
                            observed_base = int(
                                issue.get("register_list_base", "0"), 16
                            )
                            observed_count = issue.get("register_count", 0)
                            active_queues = [
                                queue
                                for queue in executing.get(
                                    "compute_queue_objects", {}
                                ).get("objects", [])
                                if queue.get("is_active_work_queue")
                            ]
                            executing["inferences"][
                                "late_global_issue_observation"
                            ] = {
                                "observed_register_list_base": (
                                    f"0x{observed_base:016x}"
                                ),
                                "observed_register_count": observed_count,
                                "active_probe_queue_count": len(active_queues),
                                "command_attributable": False,
                                "basis": (
                                    "SGX +0xc040 is a global issue port and may "
                                    "rotate to another client after the probe's "
                                    "first queue-bound launch-timeline issue"
                                ),
                            }
                write_manifest(output / "executing", executing)
                (pause / "release-shader").touch()
            wait_path(pause / "complete", process)
            complete = snapshot(
                devmem,
                entry_id,
                output,
                "complete",
                capture_physical=capture_physical,
            )
            for resource_id in sorted(submitted_ids):
                dump_resource_payload(
                    devmem, output / "complete", resource_id, complete
                )
            capture_compute_completion_records(devmem, output / "complete", complete,
                                               executing if gate_shader else submitted)
            complete["inferences"] = submitted["inferences"]
            complete["probe_command"] = command
            write_manifest(output / "complete", complete)
            write_diff(output, "created", "encoded")
            write_diff(output, "encoded", "submitted")
            if gate_shader:
                write_diff(output, "submitted", "executing")
                write_diff(output, "executing", "complete")
            write_diff(output, "submitted", "complete")
            (pause / "exit").touch()
            status = process.wait(timeout=10)
            if status != 0:
                raise RuntimeError(f"metalprobe failed with status {status}")
        finally:
            if devmem is not None:
                devmem.close()
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=5)
    print(f"captured Metal compute state in {output}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--expected-output-bytes", type=lambda v: int(v, 0),
                        help="Exact last-created output allocation size, checked against descriptor")
    parser.add_argument("--allow-sgx-mmio", action="store_true",
                        help="Explicitly opt into separately reviewed SGX MMIO reads")
    parser.add_argument("--probe", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--probe-value", type=lambda value: int(value, 0))
    parser.add_argument(
        "--ring-only",
        action="store_true",
        help=(
            "capture the exact IOGPU command ring and 0x130-byte descriptor "
            "headers without expanding the final compute descriptor"
        ),
    )
    parser.add_argument(
        "--capture-physical-gpu-state",
        action="store_true",
        help=(
            "capture the exact J713 PPL handoff, TTBAT, and selected SKU "
            "through the already-configured read-only physical oracle"
        ),
    )
    parser.add_argument(
        "--capture-usc-private-memory",
        action="store_true",
        help=(
            "at the executing shader gate, save the compute context store, "
            "used USC metadata, and every private-memory payload leaf"
        ),
    )
    parser.add_argument(
        "--capture-launch-timeline",
        action="store_true",
        help=(
            "capture every raw SGX/scheduler sample around release of a "
            "--gate-submit compute job"
        ),
    )
    parser.add_argument(
        "--launch-timeline-ms",
        type=int,
        default=250,
        help="bounded launch-timeline sampling duration (default: 250 ms)",
    )
    parser.add_argument(
        "--launch-timeline-firmware-only",
        action="store_true",
        help=(
            "capture firmware/UAT state and preflight the launch timeline "
            "without reading power-gated SGX MMIO"
        ),
    )
    parser.add_argument(
        "--ptov-address",
        type=lambda value: int(value, 0),
        help=(
            "optional cross-check for the live ml_static_ptovirt address; "
            "the exact 25G83 address is derived from DevMem's kernel anchor"
        ),
    )
    parser.add_argument("--probe-args", nargs=argparse.REMAINDER, default=[])
    args = parser.parse_args()
    try:
        if args.expected_output_bytes is not None and not 0 < args.expected_output_bytes <= MAX_PAYLOAD_LENGTH:
            raise RuntimeError("expected output size exceeds bounded collector capacity")
        if args.probe_value is not None and not 0 <= args.probe_value <= 0xFFFFFFFF:
            raise RuntimeError("--probe-value must fit uint32")
        if args.capture_usc_private_memory and not args.capture_physical_gpu_state:
            raise RuntimeError(
                "--capture-usc-private-memory requires "
                "--capture-physical-gpu-state"
            )
        if args.capture_launch_timeline:
            if not args.capture_physical_gpu_state:
                raise RuntimeError(
                    "--capture-launch-timeline requires "
                    "--capture-physical-gpu-state"
                )
            if "--gate-submit" not in args.probe_args:
                raise RuntimeError(
                    "--capture-launch-timeline requires probe argument "
                    "--gate-submit"
                )
            if not any(
                gate in args.probe_args
                for gate in ("--gate-shader", "--gate-target")
            ):
                raise RuntimeError(
                    "--capture-launch-timeline requires probe argument "
                    "--gate-shader or --gate-target"
                )
            if not 50 <= args.launch_timeline_ms <= 5000:
                raise RuntimeError("--launch-timeline-ms must be in 50..5000")
        elif args.launch_timeline_firmware_only:
            raise RuntimeError(
                "--launch-timeline-firmware-only requires "
                "--capture-launch-timeline"
            )
        capture(
            args.probe.resolve(),
            args.output.resolve(),
            args.probe_value,
            args.probe_args,
            expand_compute_tail=not args.ring_only,
            capture_physical=args.capture_physical_gpu_state,
            capture_usc_private_memory=args.capture_usc_private_memory,
            capture_launch_timeline_enabled=args.capture_launch_timeline,
            launch_timeline_mmio_enabled=(
                args.allow_sgx_mmio and not args.launch_timeline_firmware_only
            ),
            physical_mmio_enabled=(
                args.allow_sgx_mmio and not args.launch_timeline_firmware_only
            ),
            launch_timeline_ms=args.launch_timeline_ms,
            ptov_address=args.ptov_address,
            expected_output_bytes=args.expected_output_bytes,
        )
    except Exception as error:
        print(f"g16state: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
