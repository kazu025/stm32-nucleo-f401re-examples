# uart07_packet_protocol

STM32 NUCLEO-F401REのUSART2で、Circular DMA + リングバッファの上に簡単なパケット解析を追加するサンプルです。

`uart06_dma_ring_buffer`までは、受信したバイト列をそのままエコーバックしていました。このプロジェクトでは、受信データを「ただのバイト列」ではなく、`STX / LEN / SEQ / TYPE / PAYLOAD / CRC16`というパケットとして解釈します。

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

## パケット形式

このサンプルのパケット形式は次のとおりです。

```text
STX | LEN | SEQ | TYPE | PAYLOAD | CRC16
```

| フィールド | サイズ | 内容 |
| --- | ---: | --- |
| STX | 1 byte | パケット開始。固定値 `0x02` |
| LEN | 1 byte | PAYLOADの長さ。最大48バイト |
| SEQ | 1 byte | シーケンス番号。今回は受け取るだけ |
| TYPE | 1 byte | パケット種別。今回は受け取るだけ |
| PAYLOAD | 0-48 byte | 実データ |
| CRC16 | 2 byte | CRC-16/CCITT。Low byte、High byteの順 |

CRCは `STX` から `PAYLOAD` までを対象にし、初期値は `0xFFFF`、多項式は `0x1021` です。

## 処理の流れ

```text
USART2
  ↓
RX DMA circular buffer
  ↓
HAL_UARTEx_RxEventCallback()
  ↓
software ring buffer
  ↓
PacketParser_Process()
  ↓
CRC OKならpayloadだけ送信
  ↓
TX DMA
```

1. 起動時に`HAL_UARTEx_ReceiveToIdle_DMA()`を1回だけ呼びます。
2. RX DMAは64バイトの`dma_rx_buffer`へ循環受信します。
3. Half、Full、IDLEイベントで新着データだけを`ring_buffer`へコピーします。
4. mainループの`PacketParser_Process()`がリングバッファから1バイトずつ読みます。
5. `STX`を見つけたら、`LEN`、`SEQ`、`TYPE`、`PAYLOAD`、`CRC16`の順に読みます。
6. CRCが正しければ`packet_ok_count`を増やし、payloadだけをTX DMAで返します。
7. CRCが合わなければ`packet_crc_error_count`を増やして、そのパケットを捨てます。

## uart06との違い

| 項目 | uart06 | uart07 |
| --- | --- | --- |
| 受信後の扱い | バイト列をそのままエコー | パケットとして解析 |
| mainループ | リングバッファから最大64バイト読む | リングバッファから1バイトずつ読む |
| エラー検出 | なし | CRC16で検出 |
| SEQ / TYPE | なし | 受信して保持 |
| ACK / NAK | なし | まだなし |
| 再送制御 | なし | まだなし |

## デバッガで見る値

| 変数 | 見ること |
| --- | --- |
| `packet_state` | パケット解析の現在状態 |
| `packet_length` | 受信中パケットのpayload長 |
| `packet_sequence` | 受信したSEQ |
| `packet_type` | 受信したTYPE |
| `packet_ok_count` | CRC OKだったパケット数 |
| `packet_crc_error_count` | CRCエラーだったパケット数 |
| `packet_format_error_count` | LENが大きすぎた回数 |
| `ring_overflow_count` | リングバッファあふれ |

最初にブレイクポイントを置くなら、`PacketParser_ProcessByte()`のCRC判定部分と、`Packet_HandleValidPacket()`が分かりやすいです。

## ビルドと書き込み

```bash
./build.sh all
./build.sh flash
```

そのほかの操作は次で確認できます。

```bash
./build.sh help
```

## 動作確認

このプロジェクトは通常のテキストをそのまま送ってもエコーされません。決められた形式のパケットを送る必要があります。

まずはPC側から、`STX / LEN / SEQ / TYPE / PAYLOAD / CRC16`形式のバイナリを送って確認します。CRCが正しい場合だけpayloadが返ります。

```bash
./tools/send_packet.py hello
```

成功すると、次のように送ったpayloadが返ります。

```text
rx payload: b'hello'
```

`pyserial`が入っていない場合は、先にインストールしてください。

```bash
python3 -m pip install pyserial
```

## 今後の予定

- ACK / NAKを追加する
- タイムアウトと再送制御を追加する
- パケット種別ごとの処理を追加する
- 将来的にRTOSのタスク分割へ進む
