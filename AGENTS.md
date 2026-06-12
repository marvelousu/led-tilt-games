# AGENTS.md — エージェント向け開発ガイド

Arduino Nano R4 + MAX7219 LED マトリクスの傾きゲーム機。全体像は README.md、実装解説は docs/software_spec.md を参照。

## ビルド検証（コミット前に必須）

```powershell
.\check.ps1
```

- `config.h` の `MATRIX_EXT` を 0/1 両方に切り替えて全構成をコンパイルし、
  片方でも失敗すれば exit 1。config.h は終了時に必ず元へ復元される
- **8×32 / 16×32 は 1 ソース両対応が規約**。片方だけ通る変更はマージ不可。
  コード変更後は必ず check.ps1 を通してからコミットする

## arduino-cli

- 場所: PATH 上に無ければ `C:\Program Files\Arduino CLI\arduino-cli.exe`
- FQBN: `arduino:renesas_uno:nanor4`（Arduino Nano R4。Minima/WiFi ではない）
- コア未導入なら: `arduino-cli core install arduino:renesas_uno`
- 必要ライブラリ: `MD_MAX72XX`（IMU はライブラリ不使用、gyro.cpp が生 I2C）

## 音（buzzer.cpp）の編集と試聴

- 全ノートは **392Hz 以上**にする。Nano R4 の `tone()` は約 366Hz 未満で
  音程が外れる（docs/music_editing.md 参照）
- 試聴は `music_preview.html` をブラウザで開き、`buzzer.cpp` を
  ドラッグ&ドロップ → 曲名ボタンで再生。**実機レスで音を確認できる**唯一の手段
  （`piezo sim` ON で実機ブザーの鳴り方を近似）。曲を編集したら必ずここで試聴する

## 実機が必要な検証 / 不要な検証

| 不要（エージェントが完結できる） | 必要（人間 + 実機に依頼する） |
|------|------|
| コンパイル検証（check.ps1） | LED 表示の見え方・輝度 |
| 曲データの音程・長さ（music_preview.html） | 実機ブザーでの最終試聴 |
| ロジックの机上確認・コードレビュー | ジャイロ閾値の調整（docs/gyro_tuning.md） |
| | tone() の音程ずれ等タイマー系の挙動 |

実機検証ができない変更は「コンパイル緑 + 試聴/机上確認済み、実機未確認」と正直に報告すること。
