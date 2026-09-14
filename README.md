# STM32 NUCLEO-F401RE Examples

STM32 NUCLEO-F401RE を使った学習用サンプル集です。

各サンプルは STM32CubeMX で初期設定とコード生成を行い、CMake / Ninja / Arm GNU Toolchain でビルドします。書き込みには OpenOCD、シリアル通信の確認には minicom を使用します。

現在は、USART2 をポーリング方式で扱うエコーバックサンプル `uart00_polling` を収録しています。

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
└── uart00_polling/
    ├── Core/                         # アプリケーションおよびCubeMX生成コード
    ├── Drivers/                      # STM32 HAL/CMSISドライバ
    ├── cmake/                        # CMakeツールチェーン設定
    ├── CMakeLists.txt
    ├── CMakePresets.json
    ├── *.ioc                         # STM32CubeMXプロジェクト
    ├── startup_*.s                   # スタートアップコード
    ├── *.ld                          # リンカースクリプト
    ├── build.sh                      # ビルド用スクリプト
    └── flash.sh                      # Flash書き込み用スクリプト
```

`build/Debug/` などのビルド生成物は Git の追跡対象外です。

## uart00_polling

NUCLEO-F401RE の ST-LINK Virtual COM Port に接続された USART2 を使用する、ポーリング方式の UART エコーバックサンプルです。

PC から受信したデータを STM32 がポーリングで読み取り、同じデータを USART2 へ返します。UART の基本動作と、HAL を使った送受信処理を確認するための最初のサンプルです。

## ビルド

`uart00_polling` ディレクトリへ移動し、ビルドスクリプトを実行します。

```bash
cd uart00_polling
./build.sh
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
./flash.sh
```

`flash.sh` は OpenOCD とオンボード ST-LINK を使用して、ビルド済みファームウェアをマイコンへ書き込みます。書き込み前に `./build.sh` が正常に完了していることを確認してください。

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

- `uart01_irq`: USART2 の割り込み受信によるエコーバック
- DMA を使用した UART 送受信
- GPIO、タイマー、ADC など、NUCLEO-F401RE の周辺機能サンプル
- 各方式の動作や実装上の違いを比較できる説明の追加

