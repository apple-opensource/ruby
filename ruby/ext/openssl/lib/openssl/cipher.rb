=begin
= $RCSfile: cipher.rb,v $ -- Ruby-space predefined Cipher subclasses

= Info
  'OpenSSL for Ruby 2' project
  Copyright (C) 2002  Michal Rokos <m.rokos@sh.cvut.cz>
  All rights reserved.

= Licence
  This program is licenced under the same licence as Ruby.
  (See the file 'LICENCE'.)

= Version
  $Id: cipher.rb,v 1.1 2003/07/23 16:11:30 gotoyuzo Exp $
=end

##
# Should we care what if somebody require this file directly?
#require 'openssl'

module OpenSSL
  module Cipher
    %w(AES Cast5 BF DES Idea RC2 RC4 RC5).each{|cipher|
      eval(<<-EOD)
        class #{cipher} < Cipher
          def initialize(*args)
            args = args.join('-')
            if args.size == 0
              super(\"#{cipher}\")
            else
              super(\"#{cipher}-#\{args\}\")
            end
          end
        end
      EOD
    }

    class Cipher
      def random_key
        str = OpenSSL::Random.random_bytes(self.key_len)
        self.key = str
        return str
      end

      def random_iv
        str = OpenSSL::Random.random_bytes(self.iv_len)
        self.iv = str
        return str
      end
    end
  end # Cipher
end # OpenSSL
