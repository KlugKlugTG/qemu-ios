# iPhone 5 (N42AP, S5L8950/A6)

Core contributed by Steven Troughton-Smith.

The `iPhone-5` machine boots a FirmwareKit-prepared N42 kernel bundle and NAND
through launchd to SpringBoard. The board has two `apple-swift` application
processors, 1 GiB of DRAM and a 640×1136 display. UIKit uses a 320×568 point
coordinate space. Device models and the host OpenGL ES bridge are shared with
the other machines where their register and transport contracts agree.

## Supported builds

The following builds have completed fresh preparation and reached the Home
screen on their first normal boot. Each has also run stock applications,
joined the virtual Wi-Fi network and loaded web content in Safari.

| iOS | Build | Board |
| --- | --- | --- |
| 6.0 | 10A405 | N42AP |
| 7.0 | 11A465 | N42AP |
| 8.0 | 12A365 | N42AP |
| 9.0 | 13A344 | N42AP |
| 10.3.4 | 14G61 | N42AP |

The shared implementation provides touch, hardware buttons, sleep/wake, a
full charging battery, speaker audio, simulated calls and SMS, and GPS fixes
through native CoreLocation. Camera capture uses generated sensor images.
Validation applies to the listed builds; additional builds require their own
preparation and runtime checks.

## Build and launch

Use the repository's [build instructions](../../README.md#build). The host
codec dependencies used by the shared video models also apply to this target.
H.264 camera encoding requires FFmpeg with a libx264 encoder.

For an already prepared device:

```sh
build/qemu-system-arm \
  -M "iPhone-5,kboot=$DEVICE/kboot.bin,nand=$DEVICE/nand,ppn=on,baseband=on,camera=on,quiet-boot=on" \
  -m 1G -accel tcg,thread=multi \
  -display cocoa \
  -audio driver=coreaudio,out.frequency=48000 \
  -device usb-bcm-hsic,bus=usb-bus.0,port=4,mac="$WIFI_MAC" \
  -netdev user,id=wifi0 \
  -serial null \
  -qmp unix:/tmp/n42-qmp.sock,server=on,wait=off
```

`DEVICE` identifies the prepared device directory. `WIFI_MAC` must match the
WLAN address in its device tree, including the nested USB WLAN node and
`/chosen/mac-address-wifi0`. Keep the machine's CPU topology: QEMU reserves
four execution contexts for the application processors and auxiliary engines.
Use the normal TCG clock for interactive operation.

`quiet-boot=on` removes verbose flags from the in-memory boot handoff. The
prepared KBoot file remains usable for diagnostic launches. Display, PMU,
USB host, audio ports, backlight and battery models are enabled by default.
The launch above also enables the modem and camera.

## Board and device models

| Component | Implementation | Board contract |
| --- | --- | --- |
| Application processors | `apple-swift`, ARM target | Two application cores; DRAM at 0x80000000 |
| Interrupts and timers | AIC and ARM generic timers | AIC at 0x3f200000, 192 IRQs; counter and deadline conversions use the same clock period |
| GPIO | Shared S5L8930 GPIO | 29 ports, eight interrupt groups, released-input and pad-level behavior |
| Touch | Shared SPI and multitouch model | SPI1 at 0x32100000, IRQ29; Zephyr profile, ATN GPIO201 |
| I2C | PA Semi command-FIFO variant | Controllers at 0x33200000–0x33400000, IRQ18–20 |
| PMU | D1972 model | I2C0 address 0x74, GPIO6 interrupt; ADC, thermal inputs, cable state, wake and shutdown |
| Battery | Shared HDQ/bq27540 | UART5 at 0x32a00000; virtual 1440 mAh pack with external charging power |
| Accessory and backlight | TriStar and backlight models | I2C0 addresses 0x1a and 0x63 |
| USB device | Shared PHY and DWC OTG | PHY at 0x36000000; OTG at 0x36100000, IRQ11 |
| USB host | Shared EHCI/OHCI | EHCI at 0x36400000, IRQ12; modem on physical port 2, WLAN on port 4 |
| WLAN | `usb-bcm-hsic`, shared Broadcom FullMAC | BCM4334 download and runtime transport, CDC/BDC commands, Ethernet and QEMU network backend |
| Modem | `usb-mdm9x15`, shared cellular model | Qualcomm download and QMUX protocols; UART1 at 0x32600000, IRQ22 |
| Console | Shared Samsung UART | UART6 at 0x32b00000, IRQ27 |
| DMA | Shared CDMA variant | 44 channels; UART receive budgeting and audio pacing |
| Storage | Shared H2FMI and NAND store | PPN operation, persistent NAND, storage-IOP control and firmware execution |
| Display | Shared H5P display variant | Display engines at 0x3a100000 and 0x39200000; DSI at 0x39500000 |
| Scalers and DART | Shared scaler and DART models | Scalers at 0x38300000/0x38400000; DART at 0x38b00000 |
| Graphics | Shared OpenGLES bridge | Guest GL dispatch, host acceleration and IOSurface synchronization |
| Audio | MCA, I2S/CDMA, CS42L65 and CS35L19 | Speaker PCM, codec control, amplifier and DMA transport |
| Camera | H4 ISP, shared JPEG and Topaz models | Generated front/rear sensors, still capture, H.264 encoding and native playback |
| Guest services | Shared agent, package and pasteboard transports | Typed RPC, source-built guest helpers and versioned guest packages |

The peripheral models implement register and protocol behavior consumed by
the native drivers. Timing-sensitive transfers use the corresponding model's
clock and queue state. Guest-controlled descriptors, lengths, addresses and
allocation sizes must pass the shared bounds checks before use.

## Firmware preparation

FirmwareKit owns firmware selection, executable preparation, guest-package
installation and initial filesystem provisioning. A prepared device combines
matching kernel, device tree, boot arguments, System contents, Data seed,
keybag and trust metadata.

The companion FirmwareKit change provides `firmwarekit-prepare` for the N42
preparation stages. Its package README documents the required inputs, output
files and native initialization completion checks. The calling host currently
orchestrates these stages; the catalog-driven `firmwarekit create` dispatcher
does not yet select N42. Fresh preparation installs the guest-service helpers.
The separate `firmwarekit-provision-guest-tools` driver upgrades stopped legacy
images and does not implement the N42 NAND output route.

The preparation contract is:

1. Preserve N42 product identity and the original kernel/device-tree pairing.
   Populate the boot handoff and generate a fresh device-tree random seed.
2. Restore allocated filesystem contents exactly, including zero-filled
   blocks. Provision the PPN layout and persistent device metadata together.
3. Initialize protected Data with the native filesystem and MobileKeyBag
   services. Preserve ownership, modes and executable entitlements.
4. Generate the capability cache with the matching System libraries and apply
   the supported activation and finished-setup preparation for that build.
5. Install source-built guest packages using the shared preparation pipeline.
   iOS 8 and later require the matching launchd metadata and agent job policy.
6. Complete native filesystem shutdown before starting the normal guest.

The 16 GB layout uses a 3 GiB System partition beginning at 16 MiB and gives
Data the remaining 12,761,997,312 bytes. Filesystem capacity must match the
partition extent. FirmwareKit's storage-layout and native restore components
provide these dimensions and verify the restored byte stream.

For iOS 10, first-boot preparation supplies source-generated container metadata
for the MobileGestalt and configuration-profile SystemGroups. The initial
root-owned capability cache is readable by mobile. Build-specific migrator
edits retain the prepared records while allowing the remaining migrations to
run. FirmwareKit validates the original input hashes and the prepared signed
pages, and preserves executable sizes and entitlements.

User applications install through MobileInstallation onto Data. The agent
provides installation and launch control through the shared guest-service
transport. See [AppSync](../../contrib/appsync/README.md) and
[restore helpers](../../contrib/it-restore/README.md) for their source builds.

## Input and power

The digitizer profile maps absolute display coordinates into the native sensor
range with inverted sensor Y. Reports carry contact identity, lifecycle and
edge-origin information. Contact release and cancellation must retire the
active path even when the host pointer leaves the display. Multi-contact
reports share the same transport and coordinate calibration.

Hardware buttons drive the GPIO pads and the PMU wake path. A sleeping
application processor resumes through the PMU transition. The iOS 10 lock
screen uses Home to wake and a second press to open.

`system_powerdown` requests native guest shutdown. The emulator exits when the
PMU reports standby or completes the charging-halt transition. Persistence
checks require that completion before reopening the same NAND.

Battery reporting combines the HDQ gauge, PMU cable state, TriStar attachment
and the USB host's charging-current grant. The virtual pack reports healthy,
full and charging. Separate PMU thermal inputs represent a room-temperature
device and support normal camera flash operation.

## Networking, cellular and location

The WLAN transport shares `bcm_fullmac.c` with the earlier boards. The HSIC
wrapper supplies BCM4334 download framing, runtime identity, ready/reset GPIOs
and MAC provisioning. The native driver associates with `qemu-ios`, obtains a
DHCP lease and exchanges Ethernet frames through the configured QEMU backend.

The modem uses the shared phone state behind Qualcomm UART/USB transports.
Its boot protocol validates the original Mav5 payloads supplied during
preparation. Runtime service negotiation, router acknowledgement, carrier
configuration and reset state follow the native client's requests.

The simulated cellular network accepts outgoing SMS, connects calls until
hangup, and supports injected incoming calls and SMS. These events remain
inside the virtual network. Controls on `/machine/baseband-modem` include:

| Property | Value |
| --- | --- |
| `incoming-call` | Caller number |
| `incoming-sms` | `number|text` |
| `gps-fix` | `latitude,longitude[,altitude[,speed[,course[,accuracy]]]]` |
| `call-state` | Read-only call state |

Coordinates use degrees, altitude uses metres above WGS84, speed uses m/s,
course uses degrees clockwise from north and accuracy uses metres. An empty
`gps-fix` clears the fix. Native PDS sessions deliver location through
CoreLocation. See [MDM9x15](../baseband/mdm9x15.md) and
[GPS](../baseband/gps.md) for protocol details.

## Audio, camera and graphics

Speaker output uses the MCA/CDMA PCM path and the shared host audio backend.
The guest frame clock and host output sample rate are converted by the audio
model. Native incoming-call handling also uses the modeled audio ports.

The camera model generates deterministic front/rear patterns and metadata.
Rear stills are 3264×2448; front stills are 1280×960. The ISP implements channel
configuration, autofocus/exposure acknowledgement, image/statistics completion
and rear preflash/main-flash sequencing. Preview uses the video overlay and
scalers; stills use the shared JPEG engines; Topaz uses host H.264 compression.
Native Camera Roll displays the resulting photos and movies.

The graphics bridge implements ES state, buffer mapping, vertex arrays,
program uniforms, texture storage, queries, clip distances and framebuffer
fetch. IOSurface imports and readbacks preserve format, row stride, mapping
permissions and completed rendering. Cached dispatch adapters select the
correct per-context entry points in later guests.

Coherent framebuffer fetch preserves the order of overlapping primitives.
The CGL fast path uses texture barriers; large triangle draws can capture
post-transform positions and group conservatively independent primitives.
Host-only draw sections release QEMU's shared lock while retaining exclusive
renderer ownership. Guest-memory accesses and final synchronization remain
under the lock. The same implementation serves every supported board.

## Current limitations

- 3D satellite imagery supplied as HEIC material textures renders with fallback
  textures in the listed legacy guests.
- Photos editing has a reported black/warped rendering case on iOS 10.
- Native host microphone capture and full audio routing require further
  end-to-end qualification.
- The storage IOP executes with a Cortex-A9 ISA model; its physical core
  identity and complete timing fidelity require further hardware evidence.

## Validation

Run the repository gates and focused host tests:

```sh
tests/gate.sh --quick
tests/gate.sh --models
```

Relevant production slices are `tests/slice/a6-*.c`,
`tests/slice/bcm-fullmac-*.c`, `tests/slice/gles-*.c`, the shared display/DMA
checks and `tests/unit/test-ios-baseband.c`. Accelerated graphics checks need
host GPU access. The native draw-batching tests compare complete-frame pixels,
occlusion results and restored bindings against the per-primitive path.
The execution-gate test checks I/O progress, serialized GL callers and fault
retry cleanup with real host threads.

For each prepared build, verify:

1. First normal boot reaches Home, and stock applications launch.
2. Touch, edge gestures, buttons, lock, sleep and wake work.
3. Battery reports full/charging; Wi-Fi connects and Safari loads a web page.
4. A user application installs, gets its container and launches.
5. Outgoing/incoming calls and SMS complete through native applications.
6. CoreLocation receives successive fixes and handles fix removal.
7. Front/rear photos, flash modes, movie capture and playback work.
8. Native shutdown persists changes through a subsequent cold boot.

## Where preparation stands on this tree (2026-11-11)

The engine boots the N42 kernel from a kboot this fork's firmwarekit writes, and the IOP and NAND
paths come up as far as the guest's own logs show:

* `AppleIOPFMI::start` and one chip-ID read per CE (FMI0:CE0..CE3, FMI1:CE8..CE11);
* `IOFlashStorageDevice` opens, `IOFlashPartitionScheme` probes with score 4 and **publishes the whole
  table** (Boot Block, Effaceable, NVRAM, Firmware, Filesystem, System Config, Diagnostic Data, Bad
  Block Table) -- so the factory media this fork writes is read exactly as intended;
* the FMI answers every command the guest issues (`0x00`, `0x30`, `0x70`) and reports the PPN part
  (`s5l8930_iop_nand_info`: id `0x014e5050`, one CE per bus, 16384-byte pages);
* the A6's IOP core is started by the AP (`a6_iop_write` doorbell, firmware base in DRAM).

What does not happen is `AppleNANDFTL`'s own start. The panic is
`IOFlashPartitionScheme`'s wait for it ("AppleNANDFTL did not appear"), and **none** of the FTL's own
failure strings reach the console -- neither "provider is not an IOFlashStorageDevice" nor "unable to
open IOFlashStorageDevice" nor "waiting for AppleNANDFactoryBBT" -- so the kext never matches its
provider rather than failing inside it. Traces in `hw/arm/s5l8930_h2fmi.c`, `hw/arm/s5l8930_iop.c` and
`hw/arm/s5l8950.c` are gated on `LT_TRACE_RESTORE=1`.

The store this tree reads is the Swift A6NAND one (16 KiB pages, 64-byte spare, 2128 blocks per CE,
`ppn-factory`, `nand-xor-ff-v2`), one `bus<N>-ce<M>.pages` file per chip select: the engine's
`s5l8930_iop.c` opens no container, which is why firmwarekit's N42 board keeps its store raw instead of
packing it into `nand.qcow2`. The store's *size* is validated against the geometry
("NAND file size differs from geometry"), so a half-sized store cannot be used to test a different
`blocks_per_ce`.

## Where the install boot stands (2026-11-13)

With the device tree's own geometry filled (blocks-cau 1064, caus-ce 1, cau-bits 0, page-bits 8,
block-bits 11, ce-bitmap 0x0303, #ce 2 a bus, #ce-blocks 1064, default-ftl-version 1, whitening and
the 4 KiB AES chain), and a media of four chips (two CEs a bus, 1064 blocks each, whose factory header
and geometry.json carry the same numbers), the FTL does what a real one does on a factory-blank part:

```
FIL_Init [OK]
_ppnfpartReadSpecialBlock: Read: failed to locate special blocks (type = 0xC101)
WMR_Start:237 No signature found.
_DestructiveFormat: Starting VFL Format
_ppnvflFormat: dwKeepout:19, dwOptions:0x20000
_ppnvflFormat: testing special block - failed reading (0x0, 0x427, 0x0)
_fillVFLCxtBlockList: failed allocating vfl info blocks
WMR_Initial_Format failed 0x40000006
```

It **writes** the special blocks (the store trace shows programs at row 272128 on bus0's two CEs,
the top block) and its own test of them fails. What the traces say about the write path: the CDMA
channels that feed the FMI are running (ch5 16384 bytes to FMI+0x14, ch6 40 bytes to FMI+0x18, their
remainders falling 16384/40 -> 12288/32 -> 8192/20), the model accumulates them into its page buffer,
and every program carries zeros -- which is plausible for an info block, so the write itself may be
fine and the failure is in what the FTL reads back: it reads row 16432 (block 64), never the row it
wrote, so the difference is in the address each side computes for a *read*, not in the data.

Next: compare the row the guest asks for against the (cau, block, page) it means -- the row formula is
`(cau * blocks_per_cau + block) * pages_per_block + page`, and the read the format fails on names block
0x427 while the model sees row 16432.

### The failure, exactly (model trace `s5l8930_h2fmi_log`)

```
h1 program ce 0 row 0x42700 len 16424 lpn 0x0 data 00000000     <- the format writes block 0x427
h1 PPN read  row 0x4030 off0 bytes4096 meta10 ok0 data ffffffff  <- and reads block 0x40, blank
```

So the format's special-block test fails because the row it *reads* is not the row it *wrote*: it programs
block 0x427 (the top block of a 1064-block CE) and verifies block 0x40. The model serves both exactly as
asked (the write lands, the read returns the blank page), so the difference is the address the FTL
computes for a read -- the same DT geometry that made its write land where it did. Decoding the two rows
against the device tree's own knobs (blocks-cau, caus-ce, cau-bits, page-bits, block-bits, dwKeepout 19)
is what is left here.

### The row mismatch: where it actually comes from

The reads the FTL's format makes do not arrive as a command 0x30 at all: they are served from the
model's read queue (`ppn_read_row`/`ppn_read_column`), which `h2fmi_command`'s 0x7a pops. That queue is
filled at the model's *own* reads -- and the guest's own postings go through `0xb0`/`0xb8`
("PPN sequenced management-data input"), which this model treats as a *feature* write
(`MODE_PPN_FEATURE_WRITE`, `ppn_features[feature] = value`) and therefore drops as a queue entry. So a
row the FTL believes it posted is never the row the read uses, and the format verifies block 0x40 after
programming block 0x427.

Two things follow, in order: teach 0xb0/0xb8 what its payload actually carries per use (feature,
factory BBT entry, read-queue row), and only then judge the address decode -- the PPN read decode
(0x00/0x30 through the packed row) is now correct but inert on this path.

### The write path works, and the format runs (2026-11-13, late)

Three fixes took the install boot past every earlier wall:

1. **The CDMA's direction.** A channel whose direction bit (`settings` bit 1) is clear and whose
   target is a device FIFO now asks that FIFO which way it goes: room to take means the transfer is
   *into* the device, bytes to give means out of it. The A6's NAND channels carry the bit clear while
   they program the media, so their page data was being read *from* the FMI instead of written to it:
   the program buffer stayed empty and every program stored zeros. With this the trace shows the
   write buffer filling (`wdata` in the thousands) and pages landing in the store.
2. **Who may say it has room.** The FMI reports write-room only while it is armed to take data
   (control 5); without that a read's channel looked like a write and the boot tore.
3. **A blank page is a successful read.** `page_ok` means "the page has data", not "the read worked",
   and a fresh part is blank nearly everywhere -- its factory BBT among them. 0x49 for blank made the
   format die on the bad-block table it had just asked for (`Unable to get PPN feature 0x0184`).

With those, the format runs for real: `Starting VFL Format`, the media's pages are written (the store
file's mtime moves, qemu sits at 230% CPU), the special-block test passes and the read queue serves the
guest's own postings -- and then the guest panics in the AIC instead:

```
panic(cpu 1 caller 0x80a7868f): "AppleInterruptController::callPlatformFunction:
                                   unexepcted other vector number: 0x00000002"
```

That is a *new* wall, past the NAND: an interrupt the guest's controller does not expect, most likely
the CDMA's or the FMI's own line raised while the media is mid-format. Next: trace which line fires
with vector 2 (the AIC's log and the deliver path), and keep the model from raising it in that state.

### The AIC wall, with its exact shape

Past the NAND, the guest panics inside its own interrupt controller:

```
panic(cpu 0 caller 0x80a7868f): "AppleInterruptController::callPlatformFunction:
                                   unexepcted other vector number: 0x00000002"
```

The model's AIC read (offset 0x2004) answers `0x40002` for its IPI bit 31 and `0x40001` for bit 0, and
`0x10000 | vector` for device lines; the guest takes the `0x40000` class as "other" and panics on a
vector it did not set up. The IOP's own AIC shares those offsets, and its writes are now kept out of
the AP's state, but the panic survives -- so the next thing to establish is who *raises* that bit
(the kernel's own self-IPI, or a line this model raises on its behalf) and which class the A6's AIC v1
puts an IPI in. The kernel's AIC symbols are at caller 0x80a7868f in the 6.1.4 kernelcache.

#### AIC, second pass (the trace)

`-d int` names the writes: the guest itself writes SOFTINT (`+0x2008`) with `0x00000001`, `0x00000002`
and `0x80000000` (104 times in one run), and the model answers the top bit as "other vector 2"
(`0x40002`). The kernel uses that bit as "the other processors" -- it is modelled that way now -- and the
panic survived, so what is left is the *class* the A6's AIC puts an IPI in: this model invented
`0x40000 | vector` (and `0x10000 | vector` for device lines) for the A4-era VIC world, and the A6's
AppleInterruptController expects something else. The way to settle it is the guest's own kext
(`AppleInterruptController`, caller 0x80a7868f in the 6.1.4 kernelcache): disassemble around that
caller and read which class bits it switches on before it calls the unexpected-vector panic.

Tried afterwards and rejected: handing the top SOFTINT bit the per-CPU IPI's own vector (`0x40001`)
did not move the panic either, which says the *encoding itself* (this model's `0x10000 | vector` for
device lines and `0x40000 | vector` for IPIs, invented for the A4-era VIC world) is what the A6's AIC
does not share. Read it out of the guest: the panic's caller is 0x80a7868f in 6.1.4, and the kext's
text sits above the kernelcache's own `__TEXT` (0x80001000..0x802d2000), so disassemble it from the
segment that covers 0x80a7xxxx (the cache's later mappings) rather than the first segment.
