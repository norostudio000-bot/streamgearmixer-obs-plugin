# sgm-audio-source — StreamGearMixer OBS プラグイン

StreamGearMixer の STREAM ミックスを **仮想ケーブルなしで** OBS に渡す音声ソースプラグイン。

## 仕組み

```
StreamGearMixer (SharedAudioSender.h)
    └─ 名前付き共有メモリ "Local\StreamGearMixerAudioV1"
         （ロックフリーリング / float32 stereo / writePos 単調増加）
              └─ sgm-audio-source.dll（読み取り専用でアタッチ）
                   └─ obs_source_output_audio() → OBS「StreamGearMixer」ソース
```

- ミキサー側は常時書き込み（STREAM OUT デバイス設定とは独立・併存可能）
- プラグインは1秒ごとに共有メモリへの接続を試みるので、ミキサーとOBSの起動順は不問
- ミキサー再起動も自動で追従（writePos 巻き戻り検出で再同期）

## ビルド

```powershell
powershell -ExecutionPolicy Bypass -File build-plugin.ps1
```

必要なもの: Visual Studio (C++), git, インストール済み OBS Studio。
スクリプトが obs-studio のヘッダ取得・obs.lib 生成・コンパイル・インストールまで行う。

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

`Source/SharedAudioSender.h`（ミキサー側）と `sgm-audio-source.c`（本プラグイン）の
ヘッダ構造体・`SGM_DATA_OFFSET (4096)`・リングサイズ (131072 frames, 2のべき乗) を
必ず一致させる。互換性を壊す変更をするときは `version` と共有メモリ名を上げる。

（共有メモリのバイナリレイアウトは `sgm-audio-source.c` 内の `struct sgm_shm_header` と
各マクロに完全に記述されているため、本プラグインはミキサー本体のソースが無くても
単体でビルドできる。）

## ライセンス

本プラグイン `sgm-audio-source` は **GNU General Public License v2 以降（GPL-2.0-or-later）** で
配布される。これは OBS Studio の中核ライブラリ **libobs（GPLv2）にリンクする**ためで、
GPLの条件によりバイナリ（`.dll`）を配布する際は本プラグインの対応ソース一式を提供する必要がある。
本リポジトリ（`sgm-audio-source.c` / `obsconfig.h` / `build-plugin.ps1` / `README.md` / `LICENSE`）が
その対応ソースにあたる。全文は同梱の [`LICENSE`](./LICENSE) を参照。

対応ソースの公開先: https://github.com/norostudio000-bot/streamgearmixer-obs-plugin

> **重要:** GPLが適用されるのはこの OBS プラグインのみ。音声ミキサー本体 **StreamGearMixer**
> （JUCE / VST3 ベースのアプリ本体）は別プログラムであり、共有メモリ経由で疎結合されているだけなので、
> GPLの派生物には当たらない（本体は別ライセンスのまま）。

Copyright (C) 2026 Noro.
