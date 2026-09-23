# STM32 NUCLEO-F401RE Examples

STM32 NUCLEO-F401RE を使った学習用サンプル集です。

各サンプルは STM32CubeMX で初期設定とコード生成を行い、CMake / Ninja / Arm GNU Toolchain でビルドします。書き込みには OpenOCD、シリアル通信の確認には minicom を使用します。

現在は、USART2のポーリング受信、割り込み受信、RX DMA + IDLE受信、RX/TX DMA、ダブルバッファ方式を比較できる5つのエコーバックサンプルを収録しています。

## 開発環境

- Board: STM32 NUCLEO-F401RE
- MCU: STM32F401RET6
- Configuration / code generation: STM32CubeMX
- Build system: CMake + Ninja
- Compiler: Arm GNU Toolchain (`arm-none-eabi-gcc`)
- Flash / debug probe: OpenOCD + ST-LINK
- Serial terminal: minicom

各ツールが利用できることを確認してください。

```bash
cmake --version
ninja --version
arm-none-eabi-gcc --version
openocd --version
minicom --version
```

## ディレクトリ構成

```text
.
├── README.md
├── .gitignore
├── uart00_polling/                   # USART2ポーリング受信
│   ├── README.md
│   ├── Core/
│   ├── Drivers/
│   ├── cmake/
│   ├── CMakeLists.txt
│   ├── CMakePresets.json
│   ├── uart00_polling.ioc
│   ├── build.sh
│   └── flash.sh
├── uart01_irq/                       # USART2割り込み受信
│   ├── README.md
│   ├── Core/
│   ├── Drivers/
│   ├── cmake/
│   ├── CMakeLists.txt
│   ├── CMakePresets.json
│   ├── uart01_irq.ioc
│   ├── build.sh
│   └── flash.sh
├── uart03_dma_idle/                  # USART2 RX DMA + IDLE受信
│   ├── README.md
│   ├── Core/
│   ├── Drivers/
│   ├── cmake/
│   ├── CMakeLists.txt
│   ├── CMakePresets.json
│   ├── uart03_dma_idle.ioc
│   ├── build.sh
│   └── flash.sh
├── uart04_dma_tx_rx/                 # USART2 RX/TX DMA
│   ├── README.md
│   ├── Core/
│   ├── Drivers/
│   ├── cmake/
│   ├── CMakeLists.txt
│   ├── CMakePresets.json
│   ├── uart04_dma_tx_rx.ioc
│   ├── build.sh
│   └── flash.sh
└── uart05_dma_double_buffer/         # RX/TX分離バッファ
    ├── README.md
    ├── Core/
    ├── Drivers/
    ├── cmake/
    ├── CMakeLists.txt
    ├── CMakePresets.json
    ├── uart05_dma_double_buffer.ioc
    ├── build.sh
    └── flash.sh
```

`build/Debug/` などのビルド生成物は Git の追跡対象外です。

## uart00_polling

NUCLEO-F401RE の ST-LINK Virtual COM Port に接続された USART2 を使用する、ポーリング方式の UART エコーバックサンプルです。

PC から受信したデータを STM32 がポーリングで読み取り、同じデータを USART2 へ返します。UART の基本動作と、HAL を使った送受信処理を確認するための最初のサンプルです。

詳しい処理の流れは[`uart00_polling/README.md`](uart00_polling/README.md)を参照してください。

## uart01_irq

USART2で1バイト受信すると割り込みが発生し、受信完了コールバックで同じ1バイトを返します。受信待ちの間もメインループは動作し、オンボードLED（LD2）が点滅します。

詳しい割り込みの流れは[`uart01_irq/README.md`](uart01_irq/README.md)を参照してください。

## uart03_dma_idle

USART2の受信データをDMAで64バイトのバッファへ転送し、IDLE状態またはバッファ満杯を検出すると、受信した可変長データをまとめて返します。1バイトごとにCPUが処理する割り込み版との違いを確認できます。

詳しいDMAとIDLE検出の流れは[`uart03_dma_idle/README.md`](uart03_dma_idle/README.md)を参照してください。

## uart04_dma_tx_rx

USART2の受信と送信の両方にDMAを使用します。受信データを`HAL_UART_Transmit_DMA()`でエコーバックし、送信完了コールバックで次の受信を開始します。

詳しいRX/TX DMAの流れは[`uart04_dma_tx_rx/README.md`](uart04_dma_tx_rx/README.md)を参照してください。

## uart05_dma_double_buffer

RX用とTX用のバッファを分離し、受信データをTXバッファへコピーした直後に次のRX DMAを開始します。TX DMAの動作中も次のデータを受信できます。

詳しいダブルバッファの流れは[`uart05_dma_double_buffer/README.md`](uart05_dma_double_buffer/README.md)を参照してください。

## ビルド

使用するプロジェクトのディレクトリへ移動し、ビルドスクリプトを実行します。以下は割り込み版の例です。

```bash
cd uart01_irq
./build.sh all
```

スクリプトは CMake Preset を使用して構成とビルドを行います。

```bash
cmake --preset Debug
cmake --build --preset Debug
```

正常に完了すると、ELF、BIN、HEX、MAP などの成果物が `build/Debug/` 以下に生成されます。

## Flash書き込み

NUCLEO-F401RE を USB で接続してから、次を実行します。

```bash
./build.sh flash
```

ビルドスクリプトはOpenOCDとオンボードST-LINKを使用して、ファームウェアをビルドしてからマイコンへ書き込みます。

## シリアル確認

NUCLEO-F401RE を USB で接続すると、ST-LINK Virtual COM Port が Linux 上で通常 `/dev/ttyACM0` などとして認識されます。

接続先を確認します。

```bash
ls /dev/ttyACM*
```

CubeMX で USART2 に設定したボーレートを指定して minicom を起動します。以下は 115200 bps の例です。

```bash
minicom -D /dev/ttyACM0 -b 115200
```

端末から文字列を入力し、同じ内容が返ってくればエコーバックは正常です。終了するときは `Ctrl+A`、続けて `X` を入力します。

ポート名やボーレートが異なる場合は、実際の環境および `.ioc` の USART2 設定に合わせて変更してください。

## 今後の予定

- `uart02_dma`: 固定長DMA受信との比較用サンプル
- リングバッファを使った連続受信
- GPIO、タイマー、ADC など、NUCLEO-F401RE の周辺機能サンプル
- 各方式の動作や実装上の違いを比較できる説明の追加
