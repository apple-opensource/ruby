require_relative '../../../spec_helper'
require_relative 'shared/add'
require 'rexml/document'

describe "REXML::Attributes#add" do
 it_behaves_like :rexml_attribute_add, :add
end
