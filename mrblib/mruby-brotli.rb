#!ruby

module Brotli
  #
  # call-seq:
  #   encode(input, output_size = nil, output = nil, **opts) -> output
  #   encode(input, output, **opts) -> output
  #   encode(output_io, **opts) -> brotli encoder
  #   encode(output_io, **opts) { |brotli_encoder| ... } -> yield return value
  #
  # [RETURN output]
  # [RETURN brotli encoder]
  #   <strong>Must be call +finish+ (or +close+) method after all data compression</strong>.
  # [RETURN yield return value]
  # [input (String)]
  # [output = nil (String or nil)]
  # [output_size = nil (Integer or nil)]
  # [output_io (not a string)]
  # [opts (Hash)]
  #   quality:: (default: Brotli::BROTLI_DEFAULT_QUALITY)
  #   lgwin:: (default: Brotli::BROTLI_DEFAULT_WINDOW)
  #   mode:: (default: Brotli::BROTLI_DEFAULT_MODE)
  # [YIELD (brotli_encoder)]
  #
  def Brotli.encode(arg1, *args, &block)
    return Encoder.encode(arg1, *args) if arg1.kind_of?(String)

    Encoder.wrap(arg1, *args, &block)
  end

  #
  # call-seq:
  #   decode(input, output_size = nil, output = nil) -> output
  #   decode(input, output) -> output
  #   decode(input_io) -> brotli decoder
  #   decode(input_io) { |brotli_decoder| ... } -> yield return value
  #
  # [RETURN output]
  # [RETURN brotli decoder]
  # [RETURN yield return value]
  # [input (string)]
  # [output = nil (string or nil)]
  # [output_size = nil (integer or nil)]
  # [output_io (not a string)]
  #
  def Brotli.decode(arg1, *args, &block)
    return Decoder.decode(arg1, *args) if arg1.kind_of?(String)

    Decoder.wrap(arg1, *args, &block)
  end

  module StreamWrapper
    def wrap(*args)
      e = new(*args)

      return e unless block_given?

      begin
        yield e
      ensure
        e.finish
      end
    end
  end

  Compressor = Encoder
  Decompressor = Decoder
  Uncompressor = Decoder

  class << Brotli
    alias compress encode
    alias decompress decode
    alias uncompress decode
  end

  class Encoder
    extend StreamWrapper

    alias write   encode
    alias <<      encode
    alias tell    total_in
    alias pos     total_in
    alias close   finish
    alias closed? finished?
    alias eof     finished?
    alias eof?    finished?

    class << Encoder
      alias compress encode
    end
  end

  class Decoder
    extend StreamWrapper

    alias read    decode
    alias tell    total_out
    alias pos     total_out
    alias close   finish
    alias closed? finished?
    alias eof     finished?
    alias eof?    finished?

    class << Decoder
      alias decompress decode
      alias uncompress decode
    end
  end
end
