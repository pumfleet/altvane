# Training Altvane Dialogue's own model (planned)

v0.1 runs DPDFNet, which estimates speech only; the voice-reverb stem is split off with DSP and scores about half of
what an ideal mask reaches (`docs/BENCHMARK.md`). The next step is our own model with three outputs (dry voice,
voice reverb, ambience), trained only on data we may use commercially, exported with the same streaming interface
(one STFT frame and a flat state in, three complex masks out) so it drops in behind `dsp::SeparatorModel`. The
plug-in runs networks with its own C++ (`source/dsp/dpdfnet`), so a new architecture also needs its forward pass
written there (`models/README.md`).

Train it without look-ahead (`conv_lookahead` and `df_lookahead` at 0), so it can be monitored live. DPDFNet's 40 ms
look-ahead is two-thirds of today's 60 ms latency floor; without it the floor is the 20 ms analysis frame.

Nothing here is built yet. This page is what the training machine needs, so it can be set up in the meantime.

## The machine: an RTX 3070 in Proxmox

8 GB of VRAM is enough for models of this size (2.5–4M parameters, like DPDFNet) with smaller batches. Expect a
fine-tune from DPDFNet to take a few days and training from scratch one to two weeks. Data preparation (convolving,
mixing, resampling) is CPU-bound, so give the guest as many cores as you can spare, 32 GB of RAM or more, and about
500 GB of fast disk for the datasets.

Two ways to give the guest the GPU. An LXC container is lighter and lets the host keep using the card's driver; a VM
with PCIe passthrough isolates it fully but needs IOMMU and gives the card to the VM alone.

### Option A: LXC container (recommended)

On the Proxmox host, as root:

1. Install the build prerequisites and the NVIDIA driver on the host (the container will use the same version):
   ```console
   apt install pve-headers-$(uname -r) build-essential
   # download the Linux x86_64 driver .run from nvidia.com, then:
   sh NVIDIA-Linux-x86_64-<version>.run --dkms
   nvidia-smi   # the 3070 should be listed
   ```
   If `nouveau` is loaded, blacklist it (`echo "blacklist nouveau" > /etc/modprobe.d/blacklist-nouveau.conf`,
   `update-initramfs -u`, reboot) before installing.
2. Make sure the device nodes exist at boot (add `nvidia` and `nvidia_uvm` to `/etc/modules-load.d/nvidia.conf`, and
   run `nvidia-modprobe -u -c=0` once), then note their major numbers: `ls -l /dev/nvidia*` (typically 195 for
   `nvidia0`/`nvidiactl`, and a 5xx number for `nvidia-uvm`).
3. Create an Ubuntu 24.04 container (privileged is simplest for GPU access), with the cores, RAM and disk above.
4. Add to `/etc/pve/lxc/<id>.conf`, using the major numbers from step 2:
   ```
   lxc.cgroup2.devices.allow: c 195:* rwm
   lxc.cgroup2.devices.allow: c 5xx:* rwm
   lxc.mount.entry: /dev/nvidia0 dev/nvidia0 none bind,optional,create=file
   lxc.mount.entry: /dev/nvidiactl dev/nvidiactl none bind,optional,create=file
   lxc.mount.entry: /dev/nvidia-uvm dev/nvidia-uvm none bind,optional,create=file
   lxc.mount.entry: /dev/nvidia-uvm-tools dev/nvidia-uvm-tools none bind,optional,create=file
   lxc.mount.entry: /dev/nvidia-modeset dev/nvidia-modeset none bind,optional,create=file
   ```
5. Start the container and, inside it, install the same driver version without its kernel module:
   ```console
   sh NVIDIA-Linux-x86_64-<version>.run --no-kernel-module
   nvidia-smi   # should show the 3070 from inside the container
   ```

### Option B: VM with PCIe passthrough

Enable IOMMU (`intel_iommu=on` or `amd_iommu=on` on the kernel command line, plus `iommu=pt`), load the `vfio`
modules, check the 3070 sits in its own IOMMU group, then add it to an Ubuntu VM as a PCI device (All Functions, PCI
Express) and install the NVIDIA driver in the VM as on any Linux machine.

### Then

Inside the container or VM: a user with sudo, `openssh-server`, Python 3.12 and `uv`, and the CUDA-enabled PyTorch
wheel (`uv pip install torch --index-url https://download.pytorch.org/whl/cu128` or newer). Share SSH access
(a host name and a key) and the data preparation and training runs can be driven remotely from here.

## Data, licence-clean only

| Role | Datasets | Licence |
|---|---|---|
| Speech | VCTK 0.92 (48 kHz), HiFi-TTS, LibriVox recordings | CC BY 4.0, public domain |
| Ambience | DEMAND; the CC0 and CC BY clips of FSD50K (filter by each clip's licence) | CC BY-SA 3.0, CC0 / CC BY |
| Rooms | OpenSLR 26 / 28 impulse responses; rooms simulated with pyroomacoustics | Apache-2.0; generated |

Each impulse response is split about 50 ms after the direct sound: before it, the dry-voice target; after it, the
reverb target. Excluded: WHAM! and EARS (non-commercial), the non-commercial FSD50K clips, and the YouTube-sourced
parts of the DNS Challenge data (AudioSet, VoxCeleb2). The data preparation will write the licence of every file it
uses to `DATA_LICENSES.md` alongside each model it produces.
