# Tangmen（唐門）

x86_64 向けの自作 OS カーネルを、AI がゼロから実装するプロジェクトです。
同じ仕様と同じテストで、複数の言語のカーネルを作ります。

| カーネル | 言語 | 状態 |
| --- | --- | --- |
| **Kui**（趙逵） | C | M0 に着手 |
| **Yuzhu**（郁竹） | Rust | C 版が M6 に達したら着手 |
| 未定 | 3 言語目（未定） | Rust 版が M6 に達したら着手 |

名前は台湾のインディーゲーム『活俠傳』から取っています。プロジェクト名は舞台となる門派の唐門、
各カーネルは登場人物です（Rust 版の郁竹は鍛冶屋で、Rust ＝ 錆 ＝ 鉄に掛けています）。

## 目的

1. **学習と趣味**：OS の仕組みを、動くコードと解説書（`docs/milestones/`）を通じて理解する
2. **生成 AI のコーディング能力の検証**：AI が人間と同じ道具でどこまでカーネルを書けるかを、
   記録（`docs/ai-log/`）を取りながら確かめる

## 検証の条件

- 設計、実装、PR の作成、マージまで AI が行う。人間（オーナー）は読んで理解する役割
- 参照してよいもの：仕様書（Intel SDM、UEFI、Limine プロトコル、ELF、man ページなど）、OSDev Wiki などの解説記事
- 参照しないもの：既存 OS（Linux、xv6 など）のソースコード
- 同じ問題で 3 セッション行き詰まったら、オーナーに相談してよい。技術的なヒントは「人間の介入」として記録する
- モデルの切り替えはマイルストーンの区切りでのみ行い、どのマイルストーンをどのモデルで書いたかを記録する

詳しい条件と決定事項は[要件定義書](https://claude.ai/code/artifact/d8d72596-fc52-47a0-8e39-e508d1beaef7)にあります
（Claude Docs 上の非公開ドキュメントのため、オーナー以外は閲覧できません。要点はこの README にまとめています）。

## マイルストーン

| # | 名前 | 内容 | C 版（Kui） |
| --- | --- | --- | --- |
| M0 | 起動 | 開発環境、Limine での起動、シリアル出力、テスト 3 層と CI の土台 | 完了 |
| M1 | CPU の基礎 | GDT、TSS、IDT、例外、APIC、タイマー割り込み | 完了 |
| M2 | メモリ管理 | 物理ページ管理、自前のページテーブル、カーネルヒープ | 未着手 |
| M3 | マルチタスク | カーネルスレッド、コンテキストスイッチ、スケジューラ、ロック | 未着手 |
| M4 | ユーザーモード | ring 3、システムコール（Linux 互換） | 未着手 |
| M5 | プログラム実行 | ELF ローダ、VFS、initramfs | 未着手 |
| M6 | シェル | キーボード、TTY、fork/execve、画面コンソール、自作シェル（当面の目標） | 未着手 |
| M7 | ストレージ | PCI、virtio-blk、ext2 | 未着手 |
| M8 | BusyBox | Linux 用の静的リンク版 BusyBox が無改造で動く（北極星） | 未着手 |

## 使い方

必要なもの：Docker。KVM（`/dev/kvm`）があれば QEMU が高速に動きます（なくても動きます）。

ツール一式は開発用コンテナに入っているので、ホストに入れる必要はありません。
`scripts/dev <コマンド>` で、コンテナの中でコマンドを実行します。
すでに開発イメージの中（Dev Container、claude-sandbox）にいるときは、docker を使わずにそのまま実行します。

```sh
scripts/dev make run      # カーネルをビルドして QEMU で起動する（シリアル出力が端末に出る。Ctrl-A → X で終了）
scripts/dev make test     # テスト 3 層（ホスト単体、カーネル内単体、QEMU 結合）をすべて実行する
scripts/dev make debug    # QEMU を GDB の接続待ちで起動する
scripts/dev make ci       # CI と同じ一連のチェック（書式、ビルド、テスト）を実行する
```

言語は `KERNEL` で選びます（既定は `c`）。例：`scripts/dev make test KERNEL=c`

VS Code を使う場合は、`.devcontainer/` の Dev Container で開くこともできます。

## ディレクトリ構成

```
.devcontainer/      開発環境（CI も同じイメージを使う）
.github/workflows/  CI
boot/               Limine の設定（limine.conf）
docs/
  milestones/       マイルストーンごとの解説書
  ai-log/           AI 能力検証の記録
  third-party.md    外部ソフトとライセンスの一覧
kernels/
  c/                Kui（C 版）
scripts/            開発用スクリプト（コンテナ実行、QEMU 起動）
Makefile            ビルド・ISO 作成・起動・テストの入口
tests/              結合テスト（全カーネル共通）
```

## ライセンス

MIT（[LICENSE](LICENSE)）。外部ソフトウェアのライセンスは [docs/third-party.md](docs/third-party.md) を参照してください。
