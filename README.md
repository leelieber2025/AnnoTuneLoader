# AnnoTune Loader

A small window that starts the local model server for [AnnoTuneAgent](https://github.com/leelieber2025/AnnoTuneAgent). Nothing to install. Double-click the launcher for your system.

The first Run downloads [llama.cpp b12180](https://github.com/ggml-org/llama.cpp/releases/tag/b12180) for this computer. A later Run uses that copy. The server listens at `http://127.0.0.1:8080`.

| Computer | Download |
| --- | --- |
| Windows with an NVIDIA GPU | CUDA 12.4 build and its CUDA libraries |
| Windows on ARM with an NVIDIA GPU | CUDA 13.5 build and its CUDA libraries |
| Other Windows GPU | Vulkan build |
| Windows with no GPU | CPU build |
| Mac, Apple silicon | Metal build |
| Mac, Intel | Metal build for Intel |

## Start

Windows: download [AnnoTuneLoader.exe](https://github.com/leelieber2025/AnnoTuneLoader/raw/main/AnnoTuneLoader.exe) and double-click it. That is the only file you need.

Mac: double-click `AnnoTuneLoader.command`. The first time, use Control-click and choose Open.

Choose the base GGUF, the name adapter, and the ML adapter. Confirm. The same three files are offered the next time. The GPU is used when the downloaded build has one.

Leave the loader open while you annotate. Stop closes the server this loader started. A server that is already running on port 8080 is left as it is.

## License

Apache License 2.0. See [LICENSE](LICENSE).
