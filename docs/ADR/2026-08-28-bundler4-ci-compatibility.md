# ADR: CIで現行Bundlerを利用する

## 背景

Ruby 4.0の標準Bundlerは4.x系だが、gemspecの開発依存がBundler 2.xまでに制限されていた。そのため、CIでBundler 2.7.2を個別にインストールして実行する必要があった。

## 決定

- 開発依存のBundlerを`>= 2.4`かつ`< 5`に変更し、Ruby 3.2のBundler 2.xとRuby 4.0のBundler 4.xを許可する。
- CIではBundlerを個別にインストール・固定せず、`ruby/setup-ruby`が用意する環境のBundlerを使用する。
- Bundler 4で非推奨となった設定形式を使わず、`bundle config set --local path vendor/bundle`を使用する。

## 理由

CI環境の標準Bundlerを許可することで、Rubyの更新に伴うBundlerの差異をCI専用の固定値で隠さず、実際の実行環境に近い依存解決を検証できる。Bundlerのメジャー上限は、将来の互換性を無制限に仮定しないため`< 5`とする。

## 影響

Ruby 3.2 / 4.0のCIは、それぞれの環境で利用可能なBundlerを使用する。Bundler 4の導入により開発依存の解決条件は広がるが、gemの実行時依存には影響しない。
