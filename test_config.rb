#!ruby

require "yaml"

configure = YAML.load(<<-CONFIGURE)
host:
  defines:
  - MRB_INT32
  cflags:
host16-nan:
  defines:
  - MRB_INT16
  - MRB_NAN_BOXING
  cflags:
host-word:
  defines:
  - MRB_WORD_BOXING
  cflags:
CONFIGURE

configure.each_pair do |name, c|
  MRuby::Build.new(name) do |conf|
    toolchain :clang

    conf.build_dir = name

    if (d = c["defines"]) && !d.empty?
      cc.defines << Array(d)
      cxx.defines << Array(d)
    end

    enable_debug
    enable_test

    gem core: "mruby-sprintf"
    gem core: "mruby-print"
    gem core: "mruby-bin-mrbc"
    gem core: "mruby-bin-mirb"
    gem core: "mruby-bin-mruby"
    gem File.dirname(__FILE__)
  end
end
