# Ruby 4.0.6 / MP4 ItemMap SIGSEGV 調査メモ

作成日: 2026-09-01

## 対象事象

macOS arm64、Ruby 4.0.6（YJIT有効）、TagLib 2.3.1、`taglib-ruby-plus` の
`84be79df72f0b55fd427f73226ae28e2f82edc8c` を使う実運用処理で、Rubyプロセスが
SIGSEGVで終了した。

クラッシュログは `/tmp/LOG` と
`~/Library/Logs/DiagnosticReports/ruby-2026-09-01-171259.ips` に保存されている。

## 結論（現時点）

直接の障害箇所はTagLib本体ではなく、SWIG 4.1.1で生成された
`taglib_mp4.bundle` のRubyラッパーである。

特に、クラッシュした `ItemMap#insert` は第3引数の `Item` を変換する前に、
レシーバーである `ItemMap` Rubyラッパーの変換中に落ちている。したがって
`Item.from_string_list` または `Item.from_cover_art_list` の一時オブジェクト寿命は、
今回の直接原因ではない。

SWIGのtracking表がC++ポインタに対応するRuby `VALUE` をRuby GCから見えない `st_table` に
生のまま保持し、GCによるオブジェクト移動後にも利用する不具合は、最小再現で確認できた。
この不具合は元クラッシュのスタックと整合する。

ただし、実運用ログ、アプリケーションコード、読み込まれたRuby gem群から `GC.compact` または
`GC.auto_compact` を有効化する処理は見つからなかった。そのため「確認できたcompaction不具合が
実運用クラッシュの直接原因だった」とは断定しない。

## 実運用ログから確定した事項

- Rubyは `4.0.6 +YJIT +PRISM [arm64-darwin25]`。
- 読み込まれたbundleは対象コミット `84be79...` の
  `taglib_mp4.bundle`。
- Rubyレベルの経路は次の通り。

  ```text
  update_mp4_metadata
    -> FileName#add_tag
    -> TagLib::MP4::Tag#set_artwork
    -> ItemMap#_insert
  ```

- この事象では、`©cpy` 挿入ではなく `set_artwork` の
  `item_map.insert('covr', Item.from_cover_art_list(...))` で落ちた。
- Cレベルの経路は次の通り。

  ```text
  SWIG_Ruby_MangleStr
  SWIG_Ruby_ConvertPtrAndOwn
  _wrap_ItemMap__insert
  ```

- スタックに `libtag.2.dylib` は存在しない。今回の `insert` 呼び出しでは、
  TagLibの `Map::insert` を実行する前に落ちている。

## DiagnosticReports の追加照合（2026-09-01）

`~/Library/Logs/DiagnosticReports/ruby*.ips` には18件のRuby SIGSEGVレポートがあった。
このうち、次の3件は今回と同じ `SWIG_Ruby_MangleStr` →
`SWIG_Ruby_ConvertPtrAndOwn` → `_wrap_ItemMap__insert` の停止経路である。

| レポート | 発生時刻 | 親プロセス | 停止箇所 |
| --- | --- | --- | --- |
| `ruby-2026-08-29-203419.ips` | 2026-08-29 20:34 | bash | `_wrap_ItemMap__insert` |
| `ruby-2026-08-31-163930.ips` | 2026-08-31 16:39 | zsh | `_wrap_ItemMap__insert` |
| `ruby-2026-09-01-171259.ips` | 2026-09-01 17:12 | bash | `_wrap_ItemMap__insert` |

同じく `ruby-2026-09-01-164629.ips` は、同じSWIG変換経路で
`_wrap_Tag_item_map` 中に停止している。この4件は同じRuby実行バイナリのslice UUIDを持つ。
したがって、単発の入力ファイルまたは一時的な `Item` 引数だけで説明するのは難しく、
ItemMap/TagのRubyラッパー状態が壊れる仮説を強める。

一方、以下は同一原因と結論しない。

- `ruby-2026-08-28-200131.ips` は `TagLib::MP4::Atoms::Atoms` とchapter読込中に停止している。
  Rubyラッパー変換後のnative側停止であり、無効な `File` ポインタ、TagLib、または事前のheap破壊を
  追加調査する必要がある。このレポートは親プロセスが `codex` のため、実運用事象の根拠には使わない。
- `ruby-2026-08-28-201506.ips` は `Init_taglib_mp4` 中の停止であり、同じく親プロセスが `codex` である。
- 2026-09-01 17:44以降の `SWIG_Ruby_NewPointerObj` またはRuby finalizer中の複数レポートは、
  この調査で実行した `GC.compact` 最小再現によるものとして分離する。

IPSには `GC.stat[:compact_count]`、`GC.auto_compact`、YJITの有効状態、Rubyの起動引数は記録されない。
そのため、過去3件の `ItemMap#insert` クラッシュでcompactionが発生したかは、このログ群からも確定できない。

## `_wrap_ItemMap__insert` の再解析

クラッシュレポートの `_wrap_ItemMap__insert` のsymbol locationは `204`（10進数）。
対象bundleを逆アセンブルすると、関数先頭 `0x8348` から `+0xcc` の位置は、
最初の `SWIG_Ruby_ConvertPtrAndOwn` 呼び出し直後である。

```text
0x8410: SWIG_Ruby_ConvertPtrAndOwn(self, ItemMap型, ...)
0x8414: 呼び出し直後                 # クラッシュ位置

0x8520: SWIG_Ruby_ConvertPtrAndOwn(argv[1], Item型, ...)
0x8524: Item引数の変換後
```

よって、壊れていた可能性が高いのは `argv[1]` の`Item`ではなく、`self`である
`ItemMap`ラッパーである。

## 問題となるSWIG runtime

現在の生成コードはSWIG 4.1.1であり、`Data_Wrap_Struct`を使用している。

- `SWIG_RubyAddTracking` は `C++ pointer -> Ruby VALUE` を
  `st_table` に保存する。
- `SWIG_RubyInstanceFor` はその値をそのまま返す。
- `SWIG_RubyUnlinkObjects` は返されたRubyオブジェクトに対し、
  `DATA_PTR(object) = 0` を直接実行する。

関連箇所:

- `ext/taglib_mp4/taglib_mp4_wrap.cxx:1280` — tracking表への生VALUE保存
- `ext/taglib_mp4/taglib_mp4_wrap.cxx:1309` — tracking表から取得したRubyオブジェクトのunlink
- `ext/taglib_mp4/taglib_mp4_wrap.cxx:1542` — `SWIG_Ruby_NewPointerObj`
- `ext/taglib_mp4/taglib_mp4_wrap.cxx:1611` — `SWIG_Ruby_MangleStr`

`Tag#item_map` はTagLibが所有するItemMapへの借用ポインタを `SWIG_NewPointerObj` に渡す。
このため、同じItemMapへのRubyラッパーはtracking表に登録される。

また、既存Itemの置換とMP4 Fileのclose時には、コードがtracking表を利用してItem、CoverArt、
ItemMap、TagのRubyラッパーをunlinkする。

## 安定した最小再現

対象commitのインストール済みbundleをRuby 4.0.6で読み込み、次を子プロセスで実行すると
SIGSEGVで終了する。

```ruby
require 'json'
require 'taglib_plus'

file = TagLib::MP4::File.new('test/data/mp4.m4a', false)
tag = file.tag
held_item_map = tag.item_map
GC.start
GC.compact

# GC.compact後に通常のRubyオブジェクトを割り当てる。
100.times { JSON.parse('{"x":1}') }

# tracking表に登録済みの同じItemMapを再取得する。
tag.item_map
```

YJIT有効時は5回中5回、`Tag#item_map`の `SWIG_Ruby_NewPointerObj` 付近でSIGSEGVした。
YJIT有効・無効の両方で、再取得を行わず `File#close`した場合も、その後のRuby finalizerで
SIGSEGVする条件を確認した。
実運用MP4、ネットワーク処理、artworkデータは不要である。

次の対照条件は正常終了した。

- TagLibを読み込まない `JSON.parse + GC.compact`
- `GC.compact`を実行しない `TagLib + JSON.parse + File#close`
- `TagLib + GC.compact + File#close` だけの処理

JSONは原因ではなく、compaction後にRubyオブジェクトを割り当て、移動前のRubyオブジェクト領域を
再利用しやすくするための最小の負荷である。この観測は、Ruby GCが移動したラッパーに対してSWIG
runtimeがGC非追随のtracking値を使う危険性を示す強い証拠である。

この最小再現は元クラッシュと同じtracking不整合を示すが、停止位置は完全には一致しない。
最小再現は `Tag#item_map`の再取得またはfinalizerで停止し、元クラッシュは再取得後の
`ItemMap#insert`におけるself変換で停止した。allocatorとGCのタイミング差で説明可能だが、
同一原因であることは修正前後の比較で最終確認する。

## 再現しなかった条件

以下は対象bundleで成功した。成功は根本問題の否定ではなく、発現にGCタイミングや既存tracking
状態が必要であることを示す。

- `ItemMap.new` に `from_cover_art_list` を入れ、各反復で `GC.compact` を行うテスト（5,000回）
- MP4 fixtureに対する `set_artwork`（100回）
- `set_properties -> ©cpy insert -> set_artwork` の順序を強制したテスト（1,000回）
- `©cpy` と `covr` の挿入・取得を `GC.stress = true` で繰り返すテスト（3,000回）

これらはYJIT有効・無効の両方で確認した。よって、YJIT単体を根本原因とする根拠はない。

## 原因の評価

| 仮説 | 評価 | 根拠 |
| --- | --- | --- |
| TagLib本体が今回の呼び出しで落ちた | 低い | `Map::insert`到達前にSWIG変換で落ちている |
| `Item.from_*`一時オブジェクトの直接的な寿命切れ | 低い | クラッシュ位置はItem引数変換前の`ItemMap self`変換 |
| SWIG tracking / unlink処理のGC移動非対応 | 高い | source codeとGC.compact後の安定した最小プロセスSIGSEGVが整合 |
| 確認したcompaction不具合が元クラッシュの直接原因 | 未確定 | 実運用でcompactionを有効化した証拠がない |
| Ruby 4.0のGC実装変更との互換性問題 | 高い | Rubyラッパーの移動を観測し、旧Data API runtimeで失敗 |
| YJIT固有の不具合 | 低い | YJIT無効でも同種のstressテストは成功。頻度への影響は未否定 |
| 過去のTagLibまたは別native拡張による事前破壊 | 未否定 | 元クラッシュ以前のheap破壊はクラッシュログだけでは除外不可 |

Ruby 4.0はgeneric ivarオブジェクトの内部実装を変更している。これは旧Data APIラッパーを
Ruby 4で重点検証すべき追加理由である。

Ruby 4.0.6には、`GC.auto_compact`使用時にString破損またはSIGSEGVが発生するRuby本体の
既知不具合（Ruby Bug #22237）もある。この問題は4.0ブランチへbackport済みと報告されているが、
現在使用中の4.0.6には含まれていない。taglib-ruby-plus側の問題とRuby本体側の問題を分けるため、
修正済みRubyでも同じ子プロセステストを実行する必要がある。

## 確認漏れとして残っている事項

- Ruby 3.4で同じtaglib-ruby-plusをビルドした比較。ローカルにはRuby 4系しかない。
- Ruby Bug #22237修正後のRuby 4.0ブランチでの比較。
- ASan / UBSan付きdebug buildによる、compactionを使わない通常GC経路のnative heap破壊確認。
- 元クラッシュと同じ `ItemMap#insert self` で停止する最小再現。
- 全生成bundleへの影響確認。11個の `*_wrap.cxx` が同じtracking runtimeと
  `trackObjects = 1` を使用しているため、MP4だけを直すと他形式に問題を残す可能性が高い。
- 実運用時の `GC.stat[:compact_count]`。クラッシュログにはこの値が残っていないため、
  当時compactionが実行されたかは遡って確認できない。

## 修正方針

設計決定は `docs/ADR/2026-09-01-swig-tracking-moving-gc.md` に記録した。
この設計は、GC登録済みの可動参照で借用ラッパーだけを追跡し、Ruby所有ラッパーをtracking表から除外する。

優先順位は次の通り。

1. Ruby 3.2およびRuby 4でtracking表をRuby GCの移動に追随できる設計へ変更する。
2. `SWIG_RubyUnlinkObjects` が移動前または解放済みRuby `VALUE` に書き込めないようにする。
3. Ruby 4対応のTypedData runtimeへ移行する。
4. `Item.from_*`の返却値に適切な所有権を付与する。

4は`new TagLib::MP4::Item`を`SWIG_NewPointerObj(..., 0)`で返していることによるリーク対策であり、
今回の直接原因とは区別する。

SWIG 4.5はTypedData化を含むが、ローカルで確認した`rubytracking.swg`にも生の`st_table`へ
Ruby `VALUE`を保存する構造が残っている。したがって、SWIG 4.5化だけを根本修正とは見なさない。

## 必要なテスト

修正前は上記の最小再現を子プロセスで失敗確認し、修正後は正常終了を確認する。

1. MP4 FileからTagとItemMapを取得する。
2. `GC.start; GC.compact` とRubyオブジェクト割当を実行する。
3. 同じTagからItemMapを再取得し、子プロセスがSIGSEGVせず終了することを確認する。
4. `ItemMap#insert`、既存Itemの置換、`File#close`を実行する。
5. 古いItemラッパーは置換後に`ObjectPreviouslyDeleted`となることを確認する。
6. `set_properties -> ©cpy insert -> set_artwork -> save -> close` をYJIT有効・無効で実行する。
7. Ruby 3.4、Ruby 4.0.6、Ruby Bug #22237修正後のRuby 4で比較する。

## 補足

- 実運用のファイル構成は変化しており、当時の入力そのものは再現に利用できない。
- このメモは、入力再現ではなく、対象bundle・生成コード・クラッシュ位置・最小GC観測に基づく。
- 本メモ作成時点では、ソースコードおよび生成コードは変更していない。
