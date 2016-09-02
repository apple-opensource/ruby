#
# $Id: uri.rb,v 1.1.1.2 2003/05/14 13:58:49 melville Exp $
#
# Copyright (c) 2001 akira yamada <akira@ruby-lang.org>
# You can redistribute it and/or modify it under the same term as Ruby.
#

=begin

  Copyright (c) 2001 akira yamada <akira@ruby-lang.org>
  You can redistribute it and/or modify it under the same term as Ruby.

= URI - URI support for Ruby

=end

module URI
  VERSION_CODE = '000909'.freeze
  VERSION = VERSION_CODE.scan(/../).collect{|n| n.to_i}.join('.').freeze
end

=begin

== Components

  * ((<URI>)) Module
  * ((<URI::Generic>)) Class
  * ((<URI::FTP>)) Class
  * ((<URI::HTTP>)) Class
  * ((<URI::HTTPS>)) Class
  * ((<URI::LDAP>)) Class
  * ((<URI::MailTo>)) Class

=end
require 'uri/common'
require 'uri/generic'
require 'uri/ftp'
require 'uri/http'
require 'uri/https'
require 'uri/ldap'
require 'uri/mailto'
