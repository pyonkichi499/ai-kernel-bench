# 作業の引き継ぎ（2026-10-04 時点）

このファイルは、作業を中断して別の環境（Docker コンテナ内の Claude Code など）で再開するための引き継ぎメモです。
再開したセッションは、最初にこのファイルを読んでください。作業が引き継がれたら、このファイルは削除してよいです。

**再開するブランチは `c/m2-memory`。** M1 は main にマージ済み（タグ `c/m1`）。

```sh
git fetch origin
git checkout c/m2-memory   # M2 のインターフェースの下書きが main の上に積んである
```

## 1. 現在の状態

| ブランチ / タグ | 状態 |
| --- | --- |
| `main`（タグ `c/m0`、`c/m1`） | C 版 Kui の M0（起動）と M1（CPU の基礎）がマージ済み |
| `c/m2-memory` | M2（メモリ管理）のインターフェースの下書き（pmm.h / vmm.h / kheap.h）だけをコミット。main の上に積んである |

### M1 で残っている作業（M2 の PR に含める）

`docs/milestones/M1.md` の最終照合。解説書は書き上がっているが、レビュー担当 4 体による修正のあとのコードとの照合が途中で止まった。
照合すべき最終状態の変更点は次のとおり。

- lapic_eoi は lapic_init 前なら何もしない。idt.c は、処理関数のない外部割り込みにも EOI を送る
- printk は割り込み禁止中に trylock し、取れなければロックなしで出力する
- ubsan は報告の開始時に割り込みを禁止する
- mmio は PAT ビットを保持し、途中段の RW を立てる
- タイマーの較正は 3 回測った中央値
- テストの件数はホスト 59、カーネル内 37、結合 12

## 2. 次の作業：M2（メモリ管理）

下書きのヘッダは `kernels/c/include/kui/{pmm,vmm,kheap}.h`。着手前に直すべき点が2つある。

- `phys_to_virt` が mmio.h（関数宣言）と vmm.h（inline）で重複している。vmm.h に一本化し、mmio.c は vmm の上で作り直す。
- HHDM の開始位置は、定数 `0xffff800000000000` ではなく、boot_info の `hhdm_offset` を変数として使う（Limine が位置を変える可能性に備えるため）。

### 計画

- **初期化順**：gdt → idt → pmm_init → vmm_init（自前のページテーブルに切り替え）→ kheap_init → pmm_reclaim_bootloader → acpi → pic → lapic → ioapic → timer
- **起動スタック**：`_start` で静的なスタックに切り替える。スタックの下にはガードページ（割り当てない 1 ページ）を置く。
- **カーネル本体の割り当て**：text は R-X、rodata は R、data/bss は RW-NX。リンカスクリプトに区間の印を追加する。
- **物理メモリ**：バディアロケータ。純粋なロジックは src/lib に置き、ホスト単体テストで検証する。物理 0〜1MiB は除外する。
- **ヒープ**：kmalloc はスラブ方式（16〜2048 バイトの区分）。大きな確保はページを直接使う。
- **出力の文言**
  - `pmm: <total> MiB managed, <free> MiB free`
  - `vmm: kernel page tables active (pml4 0x...)`
  - `kheap: ready (<n> size classes)`
  - `pmm: reclaimed <n> KiB from bootloader`
- **自己テスト**：`selftest=write_rodata`、`exec_data`、`stack_overflow`、`null_deref`（いずれも #PF または #DF として報告されること）
- **並列の分担案**
  - A：pmm と buddy
  - B：vmm、mmio の作り直し、スタック切り替え、linker.ld
  - C：kheap、kmain、自己テスト
  - T：テスト担当。ヘッダの仕様だけを見て ktest と結合テストを書く
  - 実装後、レビュー担当 5 体（相互作用の観点を含む）
- **ルートの Makefile**：今は `OUT := $(ROOT)/build/$(KERNEL)` で固定されている。エージェントごとに出力先を分けられるよう `OUT ?=` に変え、`QEMU_ARGS` の `build/$(KERNEL)` も `$(OUT)` にする。

## 3. 進め方とルール

`CLAUDE.md` を参照（オーナーと合意した進め方、プロジェクトのルール、コミット規約をまとめてある）。

## 4. コンテナ内で再開する

次のセッションは、Docker コンテナ内の Claude Code（`claude-sandbox`）で、権限確認なしで再開する。
使い方と制約（コミットは署名なしで行い、push、PR、タグはホストで行う。QEMU は KVM で動く）は、`CLAUDE.md` の「コンテナ内で作業するとき」を参照。

- コンテナ用の準備（`.claude-sandbox.toml`、`.devcontainer/claude/Dockerfile`、`scripts/build-sandbox-image`、`CLAUDE.md` など）は、M2 とは別の PR にする。
- コンテナ内では、ホストの Claude Code のメモリは見えない。必要なことは `CLAUDE.md` とこのファイルに書いてある。
