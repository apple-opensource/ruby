#!/usr/bin/env ruby
# $RoughId: test.rb,v 1.9 2002/02/25 08:20:14 knu Exp $
# $Id: test.rb,v 1.1.1.1 2002/05/27 17:59:47 jkh Exp $

# Please only run this test on machines reasonable for testing.
# If in doubt, ask your admin.

require 'runit/testcase'
require 'runit/cui/testrunner'

# Prepend current directory to load path for testing.
$:.unshift('.')

require 'syslog'

class TestSyslog < RUNIT::TestCase
  def test_new
    assert_exception(NameError) {
      Syslog.new
    }
  end

  def test_instance
    sl1 = Syslog.instance
    sl2 = Syslog.open
    sl3 = Syslog.instance

    assert_equal(Syslog, sl1)
    assert_equal(Syslog, sl2)
    assert_equal(Syslog, sl3)
  ensure
    Syslog.close
  end

  def test_open
    # default parameters
    Syslog.open

    assert_equal($0, Syslog.ident)
    assert_equal(Syslog::LOG_PID | Syslog::LOG_CONS, Syslog.options)
    assert_equal(Syslog::LOG_USER, Syslog.facility)

    # open without close
    assert_exception(RuntimeError) {
      Syslog.open
    }

    Syslog.close

    # given parameters
    Syslog.open("foo", Syslog::LOG_NDELAY | Syslog::LOG_PERROR, Syslog::LOG_DAEMON) 

    assert_equal('foo', Syslog.ident)
    assert_equal(Syslog::LOG_NDELAY | Syslog::LOG_PERROR, Syslog.options)
    assert_equal(Syslog::LOG_DAEMON, Syslog.facility)

    Syslog.close

    # default parameters again (after close)
    Syslog.open
    Syslog.close

    assert_equal($0, Syslog.ident)
    assert_equal(Syslog::LOG_PID | Syslog::LOG_CONS, Syslog.options)
    assert_equal(Syslog::LOG_USER, Syslog.facility)

    # block
    param = nil
    Syslog.open { |param| }
    assert_equal(Syslog, param)
  ensure
    Syslog.close
  end

  def test_opened?
    assert_equal(false, Syslog.opened?)

    Syslog.open
    assert_equal(true, Syslog.opened?)

    Syslog.close
    assert_equal(false, Syslog.opened?)

    Syslog.open {
      assert_equal(true, Syslog.opened?)
    }

    assert_equal(false, Syslog.opened?)
  end

  def test_mask
    Syslog.open

    orig = Syslog.mask

    Syslog.mask = Syslog.LOG_UPTO(Syslog::LOG_ERR)
    assert_equal(Syslog.LOG_UPTO(Syslog::LOG_ERR), Syslog.mask)

    Syslog.mask = Syslog.LOG_MASK(Syslog::LOG_CRIT)
    assert_equal(Syslog.LOG_MASK(Syslog::LOG_CRIT), Syslog.mask)

    Syslog.mask = orig
  ensure
    Syslog.close
  end

  def test_log
    stderr = IO::pipe

    pid = fork {
      stderr[0].close
      STDERR.reopen(stderr[1])
      stderr[1].close

      options = Syslog::LOG_PERROR | Syslog::LOG_NDELAY

      Syslog.open("syslog_test", options) { |sl|
	sl.log(Syslog::LOG_NOTICE, "test1 - hello, %s!", "world")
	sl.notice("test1 - hello, %s!", "world")
      }

      Syslog.open("syslog_test", options | Syslog::LOG_PID) { |sl|
	sl.log(Syslog::LOG_CRIT, "test2 - pid")
	sl.crit("test2 - pid")
      }
      exit!
    }

    stderr[1].close
    Process.waitpid(pid)

    # LOG_PERROR is not yet implemented on Cygwin.
    return if RUBY_PLATFORM =~ /cygwin/

    2.times {
      assert_equal("syslog_test: test1 - hello, world!\n", stderr[0].gets)
    }

    2.times {
      assert_equal(format("syslog_test[%d]: test2 - pid\n", pid), stderr[0].gets)
    }
  end

  def test_inspect
    Syslog.open { |sl|
      assert_equal(format('<#%s: opened=%s, ident="%s", ' +
			  'options=%d, facility=%d, mask=%d>',
			  Syslog, sl.opened?, sl.ident,
			  sl.options, sl.facility, sl.mask),
		   sl.inspect)
    }
  end
end

if $0 == __FILE__
  suite = RUNIT::TestSuite.new

  suite.add_test(TestSyslog.suite)

  RUNIT::CUI::TestRunner.run(suite)
end
