#
# $Id: ldap.rb,v 1.1.1.1 2002/05/27 17:59:49 jkh Exp $
#

require 'uri/generic'

module URI

=begin

== URI::LDAP

URI::LDAP is copyrighted free software by Takaaki Tateishi and akira yamada.

  Copyright (c) 2001 Takaaki Tateishi <ttate@jaist.ac.jp> and 
  akira yamada <akira@ruby-lang.org>.
  You can redistribute it and/or modify it under the same term as Ruby.

=== Super Class

((<URI::Generic>))

=end

  # LDAP URI SCHEMA (described in RFC2255)
  # ldap://<host>/<dn>[?<attrs>[?<scope>[?<filter>[?<extensions>]]]]
  class LDAP < Generic

    DEFAULT_PORT = 389
    
    COMPONENT = [
      :scheme,
      :host, :port,
      :dn,
      :attributes,
      :scope,
      :filter,
      :extensions,
    ].freeze

    SCOPE = [
      SCOPE_ONE = 'one',
      SCOPE_SUB = 'sub',
      SCOPE_BASE = 'base',
    ].freeze

=begin

=== Class Methods

--- URI::LDAP::build

--- URI::LDAP::new

=end

    def self.build(args)
      tmp = Util::make_components_hash(self, args)

      if tmp[:dn]
	tmp[:path] = tmp[:dn]
      end

      query = []
      [:extensions, :filter, :scope, :attributes].collect do |x|
	next if !tmp[x] && query.size == 0
	query.unshift(tmp[x])
      end

      tmp[:query] = query.join('?')

      return super(tmp)
    end

    def initialize(*arg)
      super(*arg)

      if @fragment
	raise InvalidURIError, 'bad LDAP URL'
      end

      parse_dn
      parse_query
    end

    def parse_dn
      @dn = @path[1..-1]
    end
    private :parse_dn

    def parse_query
      @attributes = nil
      @scope      = nil
      @filter     = nil
      @extensions = nil

      if @query
	attrs, scope, filter, extensions = @query.split('?')

	@attributes = attrs if attrs && attrs.size > 0
	@scope      = scope if scope && scope.size > 0
	@filter     = filter if filter && filter.size > 0
	@extensions = extensions if extensions && extensions.size > 0
      end
    end
    private :parse_query

    def build_path_query
      @path = '/' + @dn

      query = []
      [@extensions, @filter, @scope, @attributes].each do |x|
	next if !x && query.size == 0
	query.unshift(x)
      end
      @query = query.join('?')
    end
    private :build_path_query

=begin

=== Instance Methods

--- URI::LDAP#dn

--- URI::LDAP#dn=(v)

=end

    def dn
      @dn
    end

    def set_dn(val)
      @dn = val
      build_path_query
    end
    protected :set_dn

    def dn=(val)
      set_dn(val)
    end

=begin

--- URI::LDAP#attributes

--- URI::LDAP#attributes=(v)

=end

    def attributes
      @attributes
    end

    def set_attributes(val)
      @attributes = val
      build_path_query
    end
    protected :set_attributes

    def attributes=(val)
      set_attributes(val)
    end

=begin

--- URI::LDAP#scope

--- URI::LDAP#scope=(v)

=end

    def scope
      @scope
    end

    def set_scope(val)
      @scope = val
      build_path_query
    end
    protected :set_scope

    def scope=(val)
      set_scope(val)
    end

=begin

--- URI::LDAP#filter

--- URI::LDAP#filter=(v)

=end

    def filter
      @filter
    end

    def set_filter(val)
      @filter = val
      build_path_query
    end
    protected :set_filter

    def filter=(val)
      set_filter(val)
    end

=begin

--- URI::LDAP#extensions

--- URI::LDAP#extensions=(v)

=end

    def extensions
      @extensions
    end

    def set_extensions(val)
      @extensions = val
      build_path_query
    end
    protected :set_extensions

    def extensions=(val)
      set_extensions(val)
    end
  end

  def hierarchical?
    false
  end

  @@schemes['LDAP'] = LDAP
end
