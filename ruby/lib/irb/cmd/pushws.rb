#
#   change-ws.rb - 
#   	$Release Version: 0.9$
#   	$Revision: 1.1.1.1 $
#   	$Date: 2003/10/15 10:11:49 $
#   	by Keiju ISHITSUKA(Nihon Rational Software Co.,Ltd)
#
# --
#
#   
#

require "irb/cmd/nop.rb"
require "irb/ext/workspaces.rb"

module IRB
  module ExtendCommand
    class Workspaces<Nop
      def execute(*obj)
	irb_context.workspaces.collect{|ws| ws.main}
      end
    end

    class PushWorkspace<Workspaces
      def execute(*obj)
	irb_context.push_workspace(*obj)
	super
      end
    end

    class PopWorkspace<Workspaces
      def execute(*obj)
	irb_context.pop_workspace(*obj)
	super
      end
    end
  end
end

