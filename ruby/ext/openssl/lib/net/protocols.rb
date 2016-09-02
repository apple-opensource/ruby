=begin
= $RCSfile: protocols.rb,v $ -- SSL/TLS enhancement for Net.

= Info
  'OpenSSL for Ruby 2' project
  Copyright (C) 2001 GOTOU YUUZOU <gotoyuzo@notwork.org>
  All rights reserved.

= Licence
  This program is licenced under the same licence as Ruby.
  (See the file 'LICENCE'.)

= Requirements
  This program requires Net 1.2.0 or higher version.
  You can get it from RAA or Ruby's CVS repository.

= Version
  $Id: protocols.rb,v 1.1.1.1 2003/10/15 10:11:47 melville Exp $
  
  2001/11/06: Contiributed to Ruby/OpenSSL project.
=end

require 'net/protocol'
require 'forwardable'
require 'openssl'

module Net
  class SSLIO < InternetMessageIO
    extend Forwardable

    def_delegators(:@ssl_context,
                   :key=, :cert=, :key_file=, :cert_file=,
                   :ca_file=, :ca_path=,
                   :verify_mode=, :verify_callback=, :verify_depth=,
                   :timeout=, :cert_store=)

    def initialize(addr, port, otime = nil, rtime = nil, dout = nil)
      super
      @ssl_context = OpenSSL::SSL::SSLContext.new()
    end

    def ssl_connect()
      @raw_socket = @socket
      @socket = OpenSSL::SSL::SSLSocket.new(@raw_socket, @ssl_context)
      @socket.connect
    end

    def close
      super
      @raw_socket.close if @raw_socket
    end

    def peer_cert
      @socket.peer_cert
    end
  end
end
