#  Delegation class that delegates even methods defined in super class,
# which can not be covered with normal method_missing hack.
#  
#  Delegator is the abstract delegation class. Need to redefine
# `__getobj__' method in the subclass.  SimpleDelegator is the 
# concrete subclass for simple delegation.
#
# Usage:
#   foo = Object.new
#   foo2 = SimpleDelegator.new(foo)
#   foo.hash == foo2.hash # => false
#
#   Foo = DelegateClass(Array)
#
#  class ExtArray<DelegateClass(Array)
#    ...
#  end

class Delegator

  def initialize(obj)
    preserved = ::Kernel.public_instance_methods(false)
    preserved -= ["to_s","to_a","inspect","==","=~","==="]
    for t in self.class.ancestors
      preserved |= t.public_instance_methods(false)
      preserved |= t.private_instance_methods(false)
      preserved |= t.protected_instance_methods(false)
      break if t == Delegator
    end
    for method in obj.methods
      next if preserved.include? method
      begin
	eval <<-EOS
	  def self.#{method}(*args, &block)
	    begin
	      __getobj__.__send__(:#{method}, *args, &block)
	    rescue Exception
	      $@.delete_if{|s| /:in `__getobj__'$/ =~ s} #`
	      $@.delete_if{|s| /^\\(eval\\):/ =~ s}
	      raise
	    end
	  end
	EOS
      rescue SyntaxError
        raise NameError, "invalid identifier %s" % method, caller(4)
      end
    end
  end

  def __getobj__
    raise NotImplementedError, "need to define `__getobj__'"
  end

end

class SimpleDelegator<Delegator

  def initialize(obj)
    super
    @obj = obj
  end

  def __getobj__
    @obj
  end

  def __setobj__(obj)
    @obj = obj
  end
end

# backward compatibility ^_^;;;
Delegater = Delegator
SimpleDelegater = SimpleDelegator

#
def DelegateClass(superclass)
  klass = Class.new
  methods = superclass.public_instance_methods(true)
  methods -= ::Kernel.public_instance_methods(false)
  methods |= ["to_s","to_a","inspect","==","=~","==="]
  klass.module_eval <<-EOS
  def initialize(obj)
    @obj = obj
  end
  EOS
  for method in methods
    begin
      klass.module_eval <<-EOS
        def #{method}(*args, &block)
	  begin
	    @obj.__send__(:#{method}, *args, &block)
	  rescue
	    $@[0,2] = nil
	    raise
	  end
	end
      EOS
    rescue SyntaxError
      raise NameError, "invalid identifier %s" % method, caller(3)
    end
  end
  return klass;
end

if __FILE__ == $0
  class ExtArray<DelegateClass(Array)
    def initialize()
      super([])
    end
  end

  ary = ExtArray.new
  p ary.class
  ary.push 25
  p ary

  foo = Object.new
  def foo.test
    25
  end
  def foo.error
    raise 'this is OK'
  end
  foo2 = SimpleDelegator.new(foo)
  p foo.test == foo2.test	# => true
  foo2.error			# raise error!
end
