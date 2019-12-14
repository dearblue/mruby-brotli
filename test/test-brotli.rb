#!ruby

# NOTE: FrozenError is defined by mruby-1.4 or later.
FrozenError = RuntimeError unless Object.const_defined?(:FrozenError)

is_mrb16 = (1 << 16).kind_of?(Float)

class Brotli::StringIO_mitaina_nanika
  attr_accessor :string
  attr_accessor :pos

  def initialize(str)
    @string = str
    @pos = 0
  end

  def read(size = nil, dest = nil)
    dest ||= ""
    dest.clear

    case size
    when 0
      return dest
    when nil
      dest << (string.byteslice(pos .. -1) or return nil)
    else
      dest << (string.byteslice(pos, size) or return nil)
    end

    @pos += dest.bytesize

    dest.bytesize > 0 ? dest : nil
  end

  def eof
    !!(pos >= string.bytesize)
  end
end

# ("a" * 16777215).brotli.brotli した結果。
# quality=5 で二重圧縮した結果、16777215 => 3222 => 69 バイトとなった。
#
# NOTE: "<<" で文字列連結を行っているのは、CRuby とは異なり mruby は
#       リテラル文字列の連結をしてくれないため
a16777215_br_br =
  "\x1b\x35\x03\x00\xe4\xff\x6e\xde\x55\xb4\x2d\x0a\x13\xbd\xa1\xde" <<
  "\x24\x9c\xcf\xe3\x70\x6c\xeb\xf9\x61\x63\x69\xf8\x53\x14\x02\x39" <<
  "\x2c\xa5\x79\x49\x0b\x00\x43\x6e\x7d\x95\x83\xca\x37\x21\x17\x9e" <<
  "\x15\x7f\x04\x55\x3f\x3a\x84\x9c\x4c\x9a\x15\xc0\x8a\x15\xee\xd8" <<
  "\xf1\x47\xe0\x1f\x41"

# ("a" * 20).brotli
a20_br = "\x1b\x13\x00\x00\x24\xc2\x62\x99\x40\x02"

# ("a" * 1).brotli
a1_br = "\x0b\x00\x80\x61\x03"

# ("").brotli
empty_br = "\x06"

s = ("123456789" * 1111)

assert "one-shot Brotli::Decoder.decode" do
  assert_raise(ArgumentError) { Brotli::Decoder.decode() }

  assert_equal "", Brotli::Decoder.decode(empty_br)
  assert_equal "a", Brotli::Decoder.decode(a1_br)
  assert_equal "a" * 20, Brotli::Decoder.decode(a20_br)
  assert_raise(RuntimeError) { Brotli::Decoder.decode("") }
  assert_raise(TypeError) { Brotli::Decoder.decode(1) }

  assert_equal "", Brotli::Decoder.decode(empty_br, 0)
  assert_raise(RuntimeError) { Brotli::Decoder.decode("", 16) }
  assert_equal "a", Brotli::Decoder.decode(a1_br, 1)
  assert_equal "", Brotli::Decoder.decode(a1_br, 0)
  assert_raise(TypeError) { Brotli::Decoder.decode(empty_br, Object.new) }
  assert_raise(TypeError) { Brotli::Decoder.decode(1, 2) }
  assert_equal "a" * 10, Brotli::Decoder.decode(a20_br, 10, partial: nil)
  assert_equal "a" * 10, Brotli::Decoder.decode(a20_br, 10, partial: true)
  assert_raise(RuntimeError) { Brotli::Decoder.decode(a20_br, 10, partial: false) }

  if is_mrb16
    a16777215_br = Brotli::Decoder.decode(a16777215_br_br)
    assert_raise(RuntimeError) { Brotli::Decoder.decode(a16777215_br).hash }
    assert_raise(RuntimeError) { Brotli::Decoder.decode(a16777215_br, nil).hash }
    assert_raise(RuntimeError) { Brotli::Decoder.decode(a16777215_br, nil, partial: nil).hash }
    assert_equal ("a" * 32766).hash, Brotli::Decoder.decode(a16777215_br, nil, partial: true).hash
    assert_raise(RuntimeError) { Brotli::Decoder.decode(a16777215_br, nil, partial: false).hash }
  end

  d = ""
  assert_equal d.object_id, Brotli::Decoder.decode(empty_br, d).object_id
  assert_equal d.object_id, Brotli::Decoder.decode(empty_br, 0, d).object_id
  assert_raise(FrozenError) { Brotli::Decoder.decode(empty_br, 0, "".freeze) }
end

assert "one-shot Brotli::Encoder.encode" do
  assert_raise(ArgumentError) { Brotli::Encoder.encode() }

  assert_equal "\x06", Brotli::Encoder.encode("")
  assert_raise(TypeError) { Brotli::Encoder.encode(Object.new) }

  assert_equal "\x06", Brotli::Encoder.encode("", 1)
  assert_equal "\x06", Brotli::Encoder.encode("", nil)
  assert_equal "\x06", Brotli::Encoder.encode("", "")
  assert_raise(TypeError) { Brotli::Encoder.encode("", Object.new) }
  assert_raise(RuntimeError) { Brotli::Encoder.encode("", 0) }
  assert_raise(RuntimeError) { Brotli::Encoder.encode("", -1) }

  assert_equal "\x06", Brotli::Encoder.encode("", nil, nil)
  assert_equal "\x06", Brotli::Encoder.encode("", nil, "")
  assert_raise(TypeError) { Brotli::Encoder.encode("", nil, Object.new) }
  assert_equal "\x06", Brotli::Encoder.encode("", 1, nil)
  assert_equal "\x06", Brotli::Encoder.encode("", 1, "")
  assert_raise(TypeError) { Brotli::Encoder.encode("", 1, Object.new) }
  assert_raise(RuntimeError) { Brotli::Encoder.encode("", -1, nil) }
  assert_raise(RuntimeError) { Brotli::Encoder.encode("", -1, "") }
  assert_raise(RuntimeError) { Brotli::Encoder.encode("", -1, Object.new) }

  d = ""
  assert_equal d.object_id, Brotli::Encoder.encode("", d).object_id
  assert_equal d.object_id, Brotli::Encoder.encode("", nil, d).object_id

  assert_raise(FrozenError) { Brotli::Encoder.encode("", "".freeze) }
  assert_raise(FrozenError) { Brotli::Encoder.encode("", nil, "".freeze) }
  assert_raise(FrozenError) { Brotli::Encoder.encode("", 1, "".freeze) }

  assert_raise(ArgumentError) { Brotli::Encoder.encode(1, 2, 3, 4) }
  assert_raise(ArgumentError) { Brotli::Encoder.encode(1, 2, 3, nil) }
  assert_raise(ArgumentError) { Brotli::Encoder.encode(1, 2, 3, Object) }

  assert_equal "\x06", Brotli::Encoder.encode("", quality: nil)
  assert_equal "\x06", Brotli::Encoder.encode("", quality: 1)
  assert_equal "\x06", Brotli::Encoder.encode("", quality: -1)
  assert_equal "\x06", Brotli::Encoder.encode("", quality: 111)
  assert_equal "\x06", Brotli::Encoder.encode("", quality: :dEfault)
  assert_equal "\x06", Brotli::Encoder.encode("", quality: :mIn)
  assert_equal "\x06", Brotli::Encoder.encode("", quality: "mAx")
  assert_equal "\x06", Brotli::Encoder.encode("", quality: :fAst)
  assert_equal "\x06", Brotli::Encoder.encode("", quality: :bEst)
  assert_raise(ArgumentError) { Brotli::Encoder.encode("", quality: :wrong_quality) }
  assert_equal "\x06", Brotli::Encoder.encode("", lgwin: nil)
  assert_equal "\x06", Brotli::Encoder.encode("", lgwin: 1)
  assert_equal "\x06", Brotli::Encoder.encode("", lgwin: -1)
  assert_equal "\x06", Brotli::Encoder.encode("", lgwin: :dEfault)
  assert_equal "\x06", Brotli::Encoder.encode("", lgwin: :mIn)
  assert_equal "\x06", Brotli::Encoder.encode("", lgwin: "mAx")
  assert_raise(ArgumentError) { Brotli::Encoder.encode("", lgwin: :wrong_lgwin) }
  assert_equal "\x06", Brotli::Encoder.encode("", mode: nil)
  assert_equal "\x06", Brotli::Encoder.encode("", mode: 1)
  assert_equal "\x06", Brotli::Encoder.encode("", mode: -1)
  assert_equal "\x06", Brotli::Encoder.encode("", mode: 111)
  assert_equal "\x06", Brotli::Encoder.encode("", mode: :gEneric)
  assert_equal "\x06", Brotli::Encoder.encode("", mode: "tExt")
  assert_equal "\x06", Brotli::Encoder.encode("", mode: :fOnt)
  assert_raise(ArgumentError) { Brotli::Encoder.encode("", mode: :wrong_mode) }

  assert_equal "\x06", Brotli::Encoder.encode("", nil, nil, quality: nil)

  assert_raise(ArgumentError) { Brotli::Encoder.encode(1, 2, 3, 4) }
  assert_raise(ArgumentError) { Brotli::Encoder.encode("", bad_keyword: nil) }
end

assert("one-shot Brotli.encode and Brotli.decode") do
  d = ""
  assert_equal String, Brotli.encode(s).class
  assert_equal String, Brotli.encode(s, 20000, quality: 1).class
  assert_equal d.object_id, Brotli.encode(s, d, quality: 1).object_id
  assert_equal d.object_id, Brotli.decode(Brotli.encode(s, quality: 1), d).object_id
  assert_equal s.hash, Brotli.decode(Brotli.encode(s, quality: 1), d).hash
  assert_equal s.byteslice(0, 2000), Brotli.decode(Brotli.encode(s, quality: 1), 2000)
end

assert("streaming Brotli::Encoder") do
  s = "123456789" * 1111 + "ABCDEFG"
  d = ""
  Brotli::Encoder.wrap(d, quality: 1) do |brotli|
    off = 0
    slicesize = 7
    while off < s.bytesize
      assert_equal off, brotli.pos
      assert_equal brotli, brotli.write(s.byteslice(off, slicesize))
      off += slicesize
      slicesize = slicesize * 3 + 4
    end
  end

  assert_equal s.hash, Brotli.decode(d, s.bytesize).hash
end

assert("streaming Brotli::Encoder (huge)") do
  skip "[mruby is build with MRB_INT16]" if is_mrb16

  s = "123456789" * 111111 + "ABCDEFG"
  d = ""
  Brotli::Encoder.wrap(d, quality: 1) do |brotli|
    off = 0
    slicesize = 7
    while off < s.bytesize
      assert_equal off, brotli.pos
      assert_equal brotli, brotli.write(s.byteslice(off, slicesize))
      off += slicesize
      slicesize = slicesize * 3 + 4
    end
  end

  assert_equal s.hash, Brotli.decode(d, s.bytesize).hash
end

assert("streaming Brotli::Decoder") do
  s = "123456789" * 1111 + "ABCDEFG"
  d = Brotli.encode(s, quality: 0)

  Brotli::Decoder.wrap(d) do |brotli|
    off = 0
    slicesize = 3
    while off < s.bytesize
      assert_equal off, brotli.total_out
      assert_equal s.byteslice(off, slicesize).hash, brotli.read(slicesize).hash
      off += slicesize
      slicesize = slicesize * 2 + 3
    end

    assert_equal nil.hash, brotli.read(slicesize).hash
  end

  istream = Brotli::StringIO_mitaina_nanika.new(d)
  Brotli.decode(istream) do |brotli|
    off = 0
    slicesize = 3
    until brotli.finished?
      assert_equal off, brotli.total_out
      assert_equal s.byteslice(off, slicesize).hash, brotli.read(slicesize).hash
      off += slicesize
      slicesize = slicesize * 2 + 3
    end

    assert_equal nil.hash, brotli.read(slicesize).hash
  end
end

assert("streaming Brotli::Decoder (huge#1)") do
  skip "[mruby is build with MRB_INT16]" if is_mrb16

  s = "123456789" * 1111111 + "ABCDEFG"
  d = Brotli.encode(s, quality: 0)

  Brotli::Decoder.wrap(d) do |brotli|
    off = 0
    slicesize = 3
    while off < s.bytesize
      assert_equal off, brotli.total_out
      assert_equal s.byteslice(off, slicesize).hash, brotli.read(slicesize).hash
      off += slicesize
      slicesize = slicesize * 2 + 3
    end

    assert_equal nil.hash, brotli.read(slicesize).hash
  end
end

if MRUBY_RELEASE_NO < 20100
  def wrap_assert
    yield
  end
else
  def wrap_assert
    assert { yield }
  end
end

assert("streaming Brotli::Decoder (huge#2)") do
  skip "[mruby is build with MRB_INT16]" if is_mrb16

  a16777215_br = nil
  assert_nothing_raised { a16777215_br = Brotli::Decoder.decode(a16777215_br_br) }
  a16777215_size = 16777215
  istream = Brotli::StringIO_mitaina_nanika.new(a16777215_br)
  brotli = nil
  assert_nothing_raised { brotli = Brotli.decode(istream) }
  off = 0
  slicesize = 3
  until brotli.finished?
    wrap_assert do
      assert_equal off, brotli.total_out
      slicesize2 = [slicesize, a16777215_size - brotli.total_out].min
      rbuf = nil
      assert_nothing_raised { rbuf = brotli.read(slicesize) }
      assert_equal ("a" * slicesize2).hash, rbuf.hash
      off += slicesize
      slicesize = slicesize * 2 + 3
    end
  end

  assert_equal nil.hash, brotli.read(slicesize).hash
end
