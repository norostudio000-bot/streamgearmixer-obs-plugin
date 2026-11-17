# sgm-audio-source — StreamGearMixer OBS プラグイン

StreamGearMixer の STREAM ミックスを **仮想ケーブルなしで** OBS に渡す音声ソースプラグイン。

## 仕組み

```
StreamGearMixer (TimedAudioSender.h)
    └─ 名前付き共有メモリ "Local\StreamGearMixerAudioV2"
         （float32 stereo / QPC時刻 / 書き込み世代番号 / 再起動epoch）
              └─ sgm-audio-source.dll（読み取り専用でアタッチ）
                   └─ obs_source_output_audio() → OBS「StreamGearMixer」ソース
```

- ミキサー側は常時書き込み（STREAM OUT デバイス設定とは独立・併存可能）
- 未接続時は250msごとに接続を試みるので、ミキサーとOBSの起動順は不問
- 接続後はMMCSS Audioスレッドと1msタイマーで読み取る。実際のスケジューリング遅延はOS負荷に依存
- チャンク末尾ではなく、フレーム位置に対応する先頭時刻をOBSへ渡す。複数チャンクの時刻重複を修正
- 再起動・サンプルレート変更はepochで識別し、以前の音声を再生しない
- 100msを超える滞留は直近10msへ復帰する（復帰時は不連続になり得る）
- V1入力へフォールバック。新本体は旧OBSプラグイン用のV1出力も併記する
- float32ステレオをそのまま渡し、追加の音量変更・NR・圧縮は行わない

## ビルド

```powershell
powershell -ExecutionPolicy Bypass -File build-plugin.ps1
# インストールも行う場合
powershell -ExecutionPolicy Bypass -File build-plugin.ps1 -Install
# ビルド済みDLLだけをインストール
powershell -ExecutionPolicy Bypass -File install-plugin.ps1
```

必要なもの: Visual Studio (C++), git, インストール済み OBS Studio。
インストール済みOBSと一致するタグのヘッダを版別に取得し、obs.libを毎回再生成する。
ビルドのみが既定。`-Install` でProgramDataへ導入する。OBSを自動終了しない。
DLL置換時は旧DLLを`.dll.bak`として保持する。OBS再起動後に新DLLがロードされる。
確認した対象はOBS Studio 32.0.4 / Windows x64。異なるメジャー版では再ビルドすること。

## インストール先

```
C:\ProgramData\obs-studio\plugins\sgm-audio-source\bin\64bit\sgm-audio-source.dll
```

OBS 32 はこの ProgramData 側ディレクトリからサードパーティプラグインをロードする
（%APPDATA%\obs-studio\plugins では読み込まれないので注意）。

## OBS のバージョンアップ時

libobs のメジャーバージョンが上がった場合（32 → 33 など）は
`build-plugin.ps1` を再実行して再ビルドすること（ヘッダのタグも自動で合わせる）。

## 共有メモリレイアウト（変更時は両側同時更新）

`sgm-protocol.h` が両側共通のワイヤー定義（MIT）。本体は `Source/TimedAudioSender.h`、
プラグインのデコーダーは `sgm-reader.h`。オフセット4096、131072フレーム、2ch。
64byteヘッダのwrite_posは旧版同様offset56。V2はsequence、end_time_ns、epochを利用する。
奇数sequenceまたは読取前後でsequence不一致なら、そのコピーは破棄して次回に再試行する。
メタデータ検証に失敗したマッピングを音声として解釈しない。
互換性を壊す変更時はversionと共有メモリ名を上げ、両側を同時更新する。

`Tests/build-tests.cmd` は実際のワーカースレッドをOBS出力スタブに接続し、作成/破棄と
ハンドル回収を試験する。実際のOBS録画・配信経路全体を検証する代わりにはならない。
本体側の `Source/Tests/audio-regression.cpp` は本物の名前付き共有メモリで
デコーダーのfloat一致・時刻・リング周回・再起動・破損ヘッダを検証する。

## ライセンス

本プラグイン `sgm-audio-source` は **GNU General Public License v2 以降（GPL-2.0-or-later）** で
配布される。これは OBS Studio の中核ライブラリ **libobs（GPLv2）にリンクする**ためで、
GPLの条件によりバイナリ（`.dll`）を配布する際は本プラグインの対応ソース一式を提供する必要がある。
対応ソース（このフォルダーのソース・ヘッダ・ビルド/導入スクリプト）は下の公開先で提供する。
StreamGearMixer のインストーラーには DLL と LICENSE（`sgm-audio-source-LICENSE.txt`）が入る。
全文は [`LICENSE`](./LICENSE) を参照。

対応ソースの公開先: https://github.com/norostudio000-bot/streamgearmixer-obs-plugin

### 対応する DLL

このソースは、2026-09-07 に作り StreamGearMixer の公開版に同梱している `sgm-audio-source.dll` のもの
（SHA-256 `AA3E0D1DA10A6447B380699CB5949445A9482ECF739464B8F4CB4AC9F2F779DE`、OBS Studio 32.0.4 のヘッダで作成）。

> **重要:** GPLが適用されるのはこの OBS プラグインのみ。音声ミキサー本体 **StreamGearMixer**
> （JUCE / VST3 ベースのアプリ本体）は別プログラムであり、共有メモリ経由で疎結合されているだけなので、
> GPLの派生物には当たらない（本体は別ライセンスのまま）。

Copyright (C) 2026 Noro.
