# ADR: MP4チャプター読み取り時のnative所有権を分離する

## 背景

TagLib 2.3.1の`MP4::File::neroChapters()`と`qtChapters()`は、`File`内部の遅延生成された`NeroChapterList`／`QtChapterList`を保持して返す。従来の形式別APIはこの戻り値を直接Ruby配列へ変換していた。一方、共通APIと`chapter_style`は一時holderで`read()`した値を利用していたため、読み取り経路によってnativeオブジェクトの寿命が異なっていた。

競合時にはC++の`ChapterList`がローカル変数として残った状態で`rb_raise`しており、Rubyの例外送出によるlongjmpとC++オブジェクトの破棄順序も安全ではなかった。

## 決定

- 形式別読み取りは`NeroChapterList`／`QtChapterList`を明示的に生成して`read()`し、`ChapterList`を値コピーしてからRuby値へ変換する。
- 共通読み取りは両形式を比較し、競合フラグをnativeオブジェクトのスコープ外へ持ち出してから`ChapterConflictError`を送出する。
- 競合時にNeroまたはQuickTimeを暗黙に採用しない。共通APIの非競合時だけ、従来どおりNeroを優先し、NeroがなければQuickTimeを返す。
- `chapter_style`、保存、削除、および形式別APIの公開仕様は変更しない。

## 理由

形式別APIでも一時holder経路を使えば、Rubyへnativeリスト内部のポインタを漏らさず、両形式の読み取り経路を統一できる。競合判定と例外送出を分離すれば、`rb_raise`時に比較用のC++リストが生存したままになることも避けられる。

## 影響

形式別読み取りは指定形式だけを読み取り、もう一方の形式との競合判定を行わない。共通APIの競合はSegmentation faultではなく`ChapterConflictError`になる。形式別読み取りでTagLibの遅延キャッシュを更新しないため、読み取り後の保存・削除処理にも影響しない。
