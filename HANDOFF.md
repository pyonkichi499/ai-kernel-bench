# 作業の引き継ぎ（2026-10-04 時点）

このファイルは、作業を中断して別の環境（Docker コンテナ内の Claude Code など）で再開するための引き継ぎメモです。
再開したセッションは、最初にこのファイルを読んでください。作業が引き継がれたら、このファイルは削除してよいです。

**再開するブランチは `c/m1-cpu`。** このファイルは `c/m1-cpu` と `c/m2-memory` にだけあり、main にはない。

```sh
git fetch origin
git checkout c/m1-cpu   # 最初は M1 の残作業（下の「M1 で残っている作業」）から
```

M1 をマージしたら、`c/m2-memory` を main に付け替えて M2 に進む（下の「2. 次の作業」）。

## 1. 現在の状態

| ブランチ / タグ | 状態 |
| --- | --- |
| `main`（タグ `c/m0`） | C 版 Kui の M0（起動）がマージ済み |
| `c/m1-cpu` | M1（CPU の基礎）の実装・レビュー・AI 記録が完了しコミット済み。push 済み、**PR は未作成** |
| `c/m2-memory` | M2（メモリ管理）のインターフェースの下書き（pmm.h / vmm.h / kheap.h）だけをコミット。`c/m1-cpu` の上に積んである |

### M1 で残っている作業

1. `docs/milestones/M1.md` の最終照合。解説書は書き上がっているが、レビュー担当 4 体による修正のあとのコードとの照合が途中で止まった。
   照合すべき最終状態の変更点は次のとおり。
   - lapic_eoi は lapic_init 前なら何もしない。idt.c は、処理関数のない外部割り込みにも EOI を送る
   - printk は割り込み禁止中に trylock し、取れなければロックなしで出力する
   - ubsan は報告の開始時に割り込みを禁止する
   - mmio は PAT ビットを保持し、途中段の RW を立てる
   - タイマーの較正は 3 回測った中央値
   - テストの件数はホスト 59、カーネル内 37、結合 12
2. README の M1 は「完了」に更新済み。
3. PR を作成し（`gh pr create --base main --head c/m1-cpu`）、CI が通ったら squash マージしてタグ `c/m1` を打つ（`git tag -m "..." c/m1`）。
4. `c/m2-memory` を main に付け替える（`git rebase --onto main c/m1-cpu c/m2-memory`）。

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

## 3. 進め方（オーナーと合意済み）

- 要件定義書（Claude Docs、非公開）：https://claude.ai/code/artifact/d8d72596-fc52-47a0-8e39-e508d1beaef7
- **時間効率だけを最大化する**。コストは問わない。並列エージェント（fork）を積極的に使う。
- **マイルストーンの区切りで止めない**。実装 → PR → マージ → 次のマイルストーン、を確認なしで続ける。
- 1 マイルストーンの流れ
  1. 統合担当がインターフェース（ヘッダ、出力の文言、make のターゲット）を先に書く
  2. 担当ファイルが重ならないように分けて、並列に実装する。テスト担当を独立させる
  3. 観点別の並列レビュー（相互作用の観点を常設）。各レビュー担当が自分の範囲を直接直す
  4. 解説書（`docs/milestones/Mx.md`）と AI 記録（`docs/ai-log/c-mx.md`）を書く
  5. PR を作り、CI が通ったら squash マージし、タグを打つ
- **パイプライン化**：M(n) のレビューと CI の間に、M(n+1) を別の git worktree で始める。
- エージェントに時間で待たせない。前の工程が終わったら統合担当が知らせる。エージェントごとに出力先を分ける。
- オーナーに決定を求めるときは、**一度に1問ずつ**（一問一答）、おすすめを添えて聞く。
- 言語は、ドキュメント、コメント、コミットメッセージ、PR のすべてで日本語（専門用語は英語）。
- **コミットメッセージと PR に Claude Code の署名を入れない**（`Co-Authored-By: Claude ...` の行、「Generated with Claude Code」など）。AI が書いたことは `docs/ai-log/` に記録する。
- **AI 能力検証のルール**
  - 既存 OS（Linux、xv6 など）のソースは読まない。仕様書と OSDev Wiki は参照してよい。
  - 同じ問題で 3 セッション行き詰まったら、オーナーに相談してよい。技術的なヒントは「人間の介入」として記録する。
  - モデルの切り替えはマイルストーンの区切りでのみ。使ったモデルを記録する。
- 名前の割り当て：プロジェクトは Tangmen、C 版は Kui（趙逵）、Rust 版は Yuzhu（郁竹、C の M6 の後に並行して開始）、3 つ目の言語は未定。

## 4. コンテナ内で再開するときの注意

- **`scripts/dev` は docker を呼ぶ**。コンテナ内で使うには、ホストの Docker ソケット（`/var/run/docker.sock`）を渡す必要がある。
  - または、開発イメージ（`.devcontainer/Dockerfile`）自体を Claude Code の実行環境にして、`scripts/dev` を使わずに `make ci` などを直接実行する。
- **KVM**：`--device /dev/kvm` と、kvm グループ（ホストの GID）の追加が必要。なくても TCG で動くが遅くなる（`TANGMEN_NO_KVM=1` で明示もできる）。
- **GPG 署名**：このリポジトリのコミットとタグは署名が必須の設定（commit.gpgsign / tag.gpgSign）。
  - コンテナ内で署名するには、鍵か gpg-agent のソケットを渡す必要がある。
  - ホストで署名していたときは、パスフレーズのキャッシュが切れていると失敗した。その場合、オーナーが別の端末で `echo test | gpg --clearsign > /dev/null` を実行するとキャッシュされる。
  - タグは `git tag -m` で作る（メッセージなしだと失敗する）。
- **GitHub**：`gh` の認証（`~/.config/gh`）と、push 用の SSH 鍵が必要。
- **Claude Docs**：要件定義書は claude.ai のアカウントに紐づく。同じアカウントでログインすれば読める。
- **git worktree**：`.git` ファイルに絶対パスが書かれるので、コンテナ内で新たに作り直すこと（ホストで使っていた `../ai-kernel-bench-m2` は削除済み）。
