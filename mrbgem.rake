#!ruby

MRuby::Gem::Specification.new("mruby-brotli") do |s|
  s.summary = "mruby bindings for brotli the compression library (unofficial)"
  s.version = "0.1"
  s.license = "BSD-2-Clause"
  s.author  = "dearblue"
  s.homepage = "https://github.com/dearblue/mruby-brotli"

  add_dependency "mruby-error",         core: "mruby-error"
  add_dependency "mruby-string-ext",    core: "mruby-string-ext"
  add_dependency "mruby-aux",           github: "dearblue/mruby-aux"

  if cc.command =~ /\b(?:g?cc|clang)\d*\b/
    cc.flags << "-Wno-tautological-constant-out-of-range-compare"
    #cc.flags << "-Wno-shift-negative-value"
    #cc.flags << "-Wno-shift-count-negative"
    #cc.flags << "-Wno-shift-count-overflow"
    #cc.flags << "-Wno-missing-braces"
  end

  if cc.defines.flatten.any? { |d| d =~ /\AHAVE_BROTLI(?=\z|=(.+))/ && ($1.nil? || $1.empty? || $1.to_i > 0) }
    cc.include_paths << "/usr/local/include"

    linker.library_paths << "/usr/local/lib"
    linker.libraries << %w(brotlicommon brotlienc brotlidec)
  else
    unless File.exist?(File.join(dir, "contrib/brotli/c"))
      Dir.chdir dir do
        system "git submodule init" or fail
        system "git submodule update" or fail
      end
    end if false

    dirp = dir.gsub(/[\[\]\{\}\,]/) { |m| "\\#{m}" }
    files = "contrib/brotli/c/{common,dec,enc}/**/*.c"
    objs.concat(Dir.glob(File.join(dirp, files)).map { |f|
      next nil unless File.file? f
      objfile f.relative_path_from(dir).pathmap("#{build_dir}/%X")
    }.compact)

    cc.include_paths.insert 0, File.join(dir, "contrib/brotli/c/include")
  end
end
