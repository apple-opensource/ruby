=begin
= $RCSfile: extconf.rb,v $ -- Generator for Makefile

= Info
  'OpenSSL for Ruby 2' project
  Copyright (C) 2002  Michal Rokos <m.rokos@sh.cvut.cz>
  All rights reserved.

= Licence
  This program is licenced under the same licence as Ruby.
  (See the file 'LICENCE'.)

= Version
  $Id: extconf.rb,v 1.1.1.1 2003/10/15 10:11:47 melville Exp $
=end

require "mkmf"

dir_config("openssl")
dir_config("kerberos")

message "=== OpenSSL for Ruby configurator ===\n"

##
# Adds -Wall -DOSSL_DEBUG for compilation and some more targets when GCC is used
# To turn it on, use: --with-debug or --enable-debug
#
if with_config("debug") or enable_config("debug")
  $defs.push("-DOSSL_DEBUG") unless $defs.include? "-DOSSL_DEBUG"

  if /gcc/ =~ CONFIG["CC"]
    $CPPFLAGS += " -Wall" unless $CPPFLAGS.split.include? "-Wall"
  end
end



message "=== Checking for system dependent stuff... ===\n"
have_library("nsl", "t_open")
have_library("socket", "socket")
have_header("unistd.h")
have_header("sys/time.h")

message "=== Checking for required stuff... ===\n"
if $mingw
  have_library("wsock32")
  have_library("gdi32")
end
result = have_header("openssl/ssl.h")
result &&= %w[crypto libeay32].any? {|lib| have_library(lib, "OpenSSL_add_all_digests")}
result &&= %w[ssl ssleay32].any? {|lib| have_library(lib, "SSL_library_init")}
if !result
  unless pkg_config("openssl") and have_header("openssl/ssl.h")
    message "=== Checking for required stuff failed. ===\n"
    message "Makefile wasn't created. Fix the errors above.\n"
    exit 1
  end
end

message "=== Checking for OpenSSL features... ===\n"
have_func("HMAC_CTX_copy")
have_func("X509_STORE_get_ex_data")
have_func("X509_STORE_set_ex_data")
have_func("EVP_MD_CTX_create")
have_func("EVP_MD_CTX_cleanup")
have_func("EVP_MD_CTX_destroy")
have_func("PEM_def_callback")
have_func("EVP_MD_CTX_init")
have_func("HMAC_CTX_init")
have_func("HMAC_CTX_cleanup")
have_func("X509_CRL_set_version")
have_func("X509_CRL_set_issuer_name")
have_func("X509_CRL_sort")
have_func("X509_CRL_add0_revoked")
have_func("CONF_get1_default_config_file")
have_func("BN_mod_sqr")
have_func("BN_mod_add")
have_func("BN_mod_sub")
have_func("BN_rand_range")
have_func("BN_pseudo_rand_range")
have_func("CONF_get1_default_config_file")
if try_cpp("#define FOO(a, ...) foo(a, ##__VA_ARGS__)\n int x(){FOO(1,2);}\n")
  $defs.push("-DHAVE_VA_ARGS_MACRO")
end
have_header("openssl/ocsp.h")
have_struct_member("EVP_CIPHER_CTX", "flags", "openssl/evp.h")

message "=== Checking for Ruby features... ===\n"
have_func("rb_obj_init_copy", "ruby.h")

message "=== Checking done. ===\n"
$distcleanfiles << "GNUmakefile" << "dep"
create_makefile("openssl")
if /gcc/ =~ CONFIG["CC"]
  File.open("GNUmakefile", "w") {|f|
    f.print <<EOD
include Makefile

SRCS = $(OBJS:.o=.c)

test-link: $(OBJS)
	$(CC) $(DLDFLAGS) #{OUTFLAG}.testlink $(OBJS) $(LIBPATH) $(LIBS) $(LOCAL_LIBS)
	@$(RM) .testlink
	@echo "Done."

dep: $(SRCS)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $^ -MM | \\
	$(RUBY) -p -e 'BEGIN{S = []' \\
		-e 'while !ARGV.empty? and /^(\\w+)=(.*)/ =~ ARGV[0]' \\
		  -e 'S << [/\#{Regexp.quote($$2)}\\//, "$$(\#{$$1})/"]' \\
		  -e 'ARGV.shift' \\
		-e 'end' \\
		-e '}' -e 'S.each(&method(:gsub!))' -- \\
            'topdir=$(topdir)' 'srcdir=$(srcdir)' 'hdrdir=$(hdrdir)' \\
	> dep

include dep
EOD
  }
end
message "Done.\n"
