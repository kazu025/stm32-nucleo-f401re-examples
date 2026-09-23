# uart06_dma_ring_buffer

STM32 NUCLEO-F401REのUSART2で、Circular DMAとソフトウェアリングバッファを使って連続受信するUARTエコーバックサンプルです。

`uart05_dma_double_buffer`では、受信イベントごとにRX DMAを再登録していました。このプロジェクトではRX DMAをCircularモードで一度だけ開始し、DMAが64バイトの受信バッファを循環している間に、新着データを256バイトのリングバッファへ退避します。

## UARTとDMAの設定

| 項目 | 設定 |
| --- | --- |
| UART | USART2 |
| TX / RX | PA2 / PA3 |
| ボーレート | 115200 bps |
| データ形式 | 8N1、フロー制御なし |
| RX DMA | DMA1 Stream5 / Channel 4 |
| RX DMAモード | Circular |
| TX DMA | DMA1 Stream6 / Channel 4 |
| TX DMAモード | Normal |
| DMA RXバッファ | 64バイト |
| リングバッファ | 256バイト（使用可能255バイト） |
| TXバッファ | 64バイト |

## 3つのバッファ

```c
static uint8_t dma_rx_buffer[DMA_RX_BUFFER_SIZE];
static uint8_t ring_buffer[RING_BUFFER_SIZE];
static uint8_t tx_buffer[TX_BUFFER_SIZE];
```

各バッファの役割は次のとおりです。

```text
USART2 → RX DMA → dma_rx_buffer（64バイトを循環）
                       ↓ 新着部分だけコピー
                  ring_buffer（受信待ち行列）
                       ↓ main()が取り出す
                  tx_buffer → TX DMA → USART2
```

## 処理の流れ

1. 起動時に`HAL_UARTEx_ReceiveToIdle_DMA()`を1回だけ呼びます。
2. RX DMAは`dma_rx_buffer`へ連続して書き込み、終端に達すると先頭へ戻ります。
3. Half、Full、IDLEのいずれかで`HAL_UARTEx_RxEventCallback()`が呼ばれます。
4. 前回のDMA書き込み位置から今回の位置までをリングバッファへ追加します。
5. mainループはリングバッファから最大64バイトを`tx_buffer`へ取り出します。
6. `HAL_UART_Transmit_DMA()`でエコーバックします。
7. 送信完了時に`tx_busy`を解除し、LD2を反転します。

RX DMAは受信イベント後も停止せず、次の受信登録も不要です。

## DMAの現在位置と前回位置

Circular DMAでは、コールバックの`Size`を単純な「今回の受信バイト数」として扱いません。`Size`はDMA受信バッファ先頭から見た現在の書き込み位置として使います。

```c
DMA_CopyNewDataToRingBuffer(Size);
```

前回位置を`dma_rx_old_position`に保存し、次の範囲だけを新着データとしてコピーします。

- 現在位置が前回位置より後ろ: `前回位置`から`現在位置`まで
- 現在位置が前回位置より前: `前回位置`から末尾、および先頭から`現在位置`まで
- 現在位置と前回位置が同じ: 重複イベントなのでコピーしない

Fullイベントでは`Size`が64になるため、コピー後の前回位置を0へ戻して次の周回に備えます。

## Halfイベントを有効にする理由

このサンプルではDMA Half Transfer割り込みを無効にしていません。64バイトのDMAバッファが半分の32バイトまで進んだ時点でも、新着データをリングバッファへ移せるようにするためです。

IDLEが発生しない連続データでも、32バイト単位でリングバッファへ退避できます。

## リングバッファ

`ring_head`は割り込み側が新しいデータを格納する位置、`ring_tail`はmain側がデータを取り出す位置です。

```text
割り込み側: dma_rx_buffer → ring_buffer[ring_head]
main側    : ring_buffer[ring_tail] → tx_buffer
```

`head`の次が`tail`に追いついた場合は満杯です。未送信データを上書きせず、新着データを捨てて`ring_overflow_count`を増やします。このため、256バイトの配列のうち実際に保持できるのは255バイトです。

## uart05との違い

| 項目 | uart05 | uart06 |
| --- | --- | --- |
| RX DMAモード | Normal | Circular |
| RX DMA開始 | 受信イベントごと | 起動時に1回 |
| 受信データの待ち行列 | なし | リングバッファ |
| 保持できる未処理データ | RXバッファ1回分 | 最大255バイト |
| Halfイベント | 無効 | 有効 |
| 連続受信への対応 | 限定的 | より強い |

## オーバーフローについて

リングバッファが満杯になるより速くデータが届き続けると、`ring_overflow_count`が増えます。デバッガでこの値が0のままであることを確認してください。

このサンプルは受信を途切れさせない仕組みを学ぶものですが、UARTの受信速度と送信速度は同じ115200 bpsです。長時間の連続入力、デバッガ停止、処理の追加などによって、リングバッファが満杯になる可能性はあります。

## ビルドと書き込み

```bash
./build.sh all
./build.sh flash
```

そのほかの操作は次で確認できます。

```bash
./build.sh help
```

## シリアル確認

```bash
minicom -D /dev/ttyACM0 -b 115200
```

文字列を入力または貼り付け、同じ内容が返ることを確認します。TX DMA完了ごとにLD2が反転します。

## デバッガで確認するポイント

1. `dma_rx_old_position`が0、32、またはIDLE検出位置へ変化すること
2. `ring_head`が受信に合わせて進むこと
3. `ring_tail`が送信データの取り出しに合わせて進むこと
4. RX DMAがイベント後もCircularモードで動き続けること
5. 送信中は`tx_busy`が1になること
6. `HAL_UART_TxCpltCallback()`で`tx_busy`が0になること
7. 通常動作で`ring_overflow_count`が0のままであること
