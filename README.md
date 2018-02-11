# mruby-brotli - mruby bindings for brotli the compression library (unofficial)

mruby へ brotli 圧縮機能を提供します。

brotli データのメモリ間圧縮・伸長およびストリーミング圧縮・伸長が行なえます。

brotli ライブラリを利用しています。


## HOW TO INSTALL

``build_config.rb`` に gem として追加して、mruby をビルドして下さい。

```ruby
MRuby::Build.new do |conf|
  conf.gem "mruby-brotli", github: "dearblue/mruby-brotli"
end
```


## HOW TO USAGE

### 圧縮 (one-shot compression)

```ruby
input = "abcdefg"
output = Brotli.encode(input)
```

``quality`` や ``lgwin``、``mode`` を指定したい場合は、キーワード引数として与えます。

```ruby
input = "abcdefg"
q = :min
w = :max
m = :font
output = Brotli.encode(input, quality: q, lgwin: w, mode: m)
```

  * ``Brotli.encode(input, maxout = nil, output = nil, **opts) -> output``<br>
    ``Brotli.encode(input, output, **opts) -> output``<br>
    ``Brotli::Encoder.encode(input, maxout = nil, output = nil, **opts) -> output``<br>
    ``Brotli::Encoder.encode(input, output, **opts) -> output``
      * 戻り値:: 引数で指定した output、または省略か nil を与えた場合は文字列オブジェクトを返す。
      * 引数 ``input``:: 圧縮したい、バイナリデータとみなされる文字列オブジェクトを指定する。
      * 引数 ``maxout``:: 圧縮後のする最大バイト長を指定する。nil を与えた場合は input の長さから自動で決定される。
      * 引数 ``output``:: 圧縮されたバイナリデータを格納する文字列オブジェクト。nil を与えた場合は、内部でからの文字列オブジェクトが用意される。
      * 引数 ``opts``:: キーワード引数
          * ``quality: nil``:: 0..11, ``:min``, ``:max``, ``:fast``, ``:best`` or ``nil``
          * ``lgwin: nil``:: ``:min``, ``:max``, ``Brotli::BROTLI_MIN_WINDOW_BITS`` .. ``Brotli::BROTLI_MAX_WINDOW_BITS`` or ``nil``
          * ``mode: nil``:: ``:general``, ``:text``, ``:font`` or ``nil``

### 伸長 (one-shot decompression)

```ruby
input_br = ...
output = Brotli.decode(input_br)
```

全体ではなく最初から途中まで伸長したい場合は、第二引数に正の整数値を与えます。

```ruby
input_br = ...
output = "" # 使い回しのためのオブジェクト (任意)
Brotli.decode(input_br, 20, output)
```

伸長する最大値を指定し、かつ本来の長さがそれを越えた場合に例外を起こしたいならば、``partial`` キーワード引数に ``false`` を指定します。

```ruby
input_br = ...
output = Brotli.decode(input_br, 20, partial: false)
```

  * ``Brotli.decode(input, maxout = nil, output = nil, **opts) -> output``<br>
    ``Brotli.decode(input, output, **opts) -> output``<br>
    ``Brotli::Decoder.decode(input, maxout = nil, output = nil, **opts) -> output``<br>
    ``Brotli::Decoder.decode(input, output, **opts) -> output``
      * 戻り値:: 伸長した結果を格納した、引数で指定した output を返す。
      * 引数 input:: brotli で圧縮したバイナリデータとしての文字列オブジェクト。
      * 引数 maxout:: 伸長する最大バイト長。nil を与えた場合は全体を伸長する。
      * 引数 output:: 伸長後のデータを格納する文字列オブジェクト。省略、あるいは nil を与えた場合は内部で生成される。
      * 引数 opts:: キーワード引数
          * ``partial: nil``:: output が不足した場合に成功させるか、例外を起こすかを指定する。<br>
            maxout に整数値を与えた場合、``partial: nil`` と ``partial: true`` は等価になる。<br>
            maxout に nil を与えた、または省略した場合、``partial: nil`` と ``partial: false`` は等価になる。

### ストリーミング圧縮 (streaming compression)

```ruby
file = File.open("data.br", "wb")
br = Brotli.encode(file, quality: :max, lgwin: :max, mode: :text)
br << any_data
br.finish
file.close
```

ブロックでくくることも出来ます。

```ruby
File.open("data.br", "wb") do |file|
  Brotli.encode(file, quality: :max, lgwin: :max, mode: :text) do |br|
    br << any_data
    # not required ``br.finish``
  end
end
```

  * ``Brotli.encode(output_stream, **opts) -> brotli encoder``<br>
    ``Brotli.encode(output_stream, **opts) { |brotli_encoder| ... } -> yield returned value``<br>
    ``Brotli::Encoder.wrap(output, **opts) -> brotli encoder``<br>
    ``Brotli::Encoder.wrap(output, **opts) { |brotli_encoder| ... } -> yield returned value``<br>
    ``Brotli::Encoder.new(output, **opts) -> brotli encoder``
      * 引数 output_stream:: 圧縮後のバイナリデータが出力される、文字列以外の任意のオブジェクト。
      * 引数 output:: 圧縮後のバイナリデータが出力される、任意のオブジェクト。
      * 引数 opts:: キーワード引数。one-shot compression を参照のこと。
  * ``Brotli::Encoder#encode(data) -> brotli encoder``
      * aliases:: ``write`` ``<<``
  * ``Brotli::Encoder#flush -> brotli encoder``
  * ``Brotli::Encoder#finish -> nil``
      * aliases:: ``close``
  * ``Brotli::Encoder#finished? -> true or false``
  * ``Brotli::Encoder#total_in -> number``
      * aliases:: ``pos`` ``tell``
  * ``Brotli::Encoder#total_out -> number``

### ストリーミング伸長 (streaming decompression)

```ruby
file = File.open("data.br", "rb")
br = Brotli.decode(file)
br.decode(10)
br.decode(100, buffer)
br.decode(nil, buffer) # full decompression
br.decode  # => nil ; already reached EOF
br.finish
file.close
```

ブロックでくくることも出来ます。

```ruby
File.open("data.br", "rb") do |file|
  Brotli.decode(file) do |br|
    br.decode(10)
    br.decode(100, buffer)
    br.decode(nil, buffer) # full decompression
    br.decode  # => nil
  end
end
```

  * ``Brotli.decode(input_stream) -> brotli decoder``<br>
    ``Brotli.decode(input_stream) { |brotli_decoder| ... } -> yield returned value``<br>
    ``Brotli::Decoder.wrap(input) -> brotli decoder``<br>
    ``Brotli::Decoder.wrap(input) { |brotli_decoder| ... } -> yield returned value``<br>
    ``Brotli::Decoder.new(input) -> brotli decoder``
      * 引数 input_stream:: 入力元の brotli ストリームとなる、文字列以外の任意のオブジェクト。``.read`` メソッドが必要。
      * 引数 input:: 入力元の brotli ストリームとなる、任意のオブジェクト。``.read`` メソッドが必要。
  * ``Brotli::Decoder#decode(size = nil, output = nil) -> output``<br>
    IO#read の挙動を模倣している。
      * aliases:: ``read``
  * ``Brotli::Decoder#finish -> nil``
      * aliases:: ``close``
  * ``Brotli::Decoder#finished? -> true or false``
      * aliases:: ``eof`` ``eof?`` ``closed?``
  * ``Brotli::Decoder#total_in -> number``
  * ``Brotli::Decoder#total_out -> number``
      * aliases:: ``pos`` ``tell``


## Specification

  * Package name: [mruby-brotli](https://github.com/dearblue/mruby-brotli)
  * Version: 0.1
  * Product quality: PROTOTYPE, UNSTABLE
  * Author: [dearblue](https://github.com/dearblue)
  * Project page: <https://github.com/dearblue/mruby-brotli>
  * Licensing: [2 clause BSD License](LICENSE)
  * Language feature requirements:
      * generic selection (C11)
      * compound literals (C99)
      * flexible array (C99)
      * variable length arrays (C99)
      * designated initializers (C99)
  * Dependency external mrbgems:
      * [mruby-aux](https://github.com/dearblue/mruby-aux)
        under [2 clause BSD License](https://github.com/dearblue/mruby-aux/blob/master/LICENSE)
        by [dearblue](https://github.com/dearblue)
      * [mruby-aux-scanhash](https://github.com/dearblue/mruby-aux-scanhash)
        under [Creative Commons Zero License \(CC0\)](https://github.com/dearblue/mruby-aux-scanhash/blob/master/LICENSE)
        by [dearblue](https://github.com/dearblue)
  * Bundled C libraries (git-submodules):
      * [brotli](https://github.com/google/brotli)-[1.0.2](https://github.com/google/brotli/blob/v1.0.2)
        under [MIT License](https://github.com/google/brotli/blob/v1.0.2/LICENSE)
        by [Google](https://github.com/google)
