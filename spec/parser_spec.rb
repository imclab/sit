#!/usr/bin/env ruby -w
# encoding: UTF-8
require "rubygems"
require "bundler/setup"
require "rspec"
require "json"
require "rr"
require File.dirname(__FILE__) + "/../sit"
require File.dirname(__FILE__) + "/abc_tsv_parser"
include Sit

describe "Tokenizer" do
	before do
	  $events = []
	end
  
	it "should do whitespace" do
	  @parser = Parser.new_whitespace
	  @parser.receiver = TestEvents.new
	  @parser.consume("hello world")
	  $events.should == [
			[:term, 0, 5, 0],
		]
		@parser.end_stream
		$events.should == [
			[:term, 0, 5, 0],
			[:term, 6, 5, 1]
		]
  end
end

describe "Parser" do
  
  before do
    $events = []
    @parser = AbcTsvParser.new
    @parser.receiver = TestEvents.new
  end
  
  it "should work standalone" do
    @parser.consume("z\ty")
    $events.should == [
      [:field, "a"], 
      [:term, 0, 1, 0], 
      [:field, "b"]
    ]
    $events.clear
    @parser.consume("yyy\tx\n")
    $events.should == [
      [:term, 2, 4, 0], 
      [:field, "c"],
      [:term, 7, 1, 0],
      [:field, "columns"],
      [:int, 3],
      [:doc, 0, 9]
    ]
    $events.clear
    @parser.consume("y\n\n\nx x\n")
    $events.should == [
        [:term, 9, 1, 0],
        [:field, "columns"],
        [:int, 1],
        [:doc, 9, 2],
        [:field, "columns"],
        [:int, 1],
        [:doc, 11, 1],
        [:field, "columns"],
        [:int, 1],
        [:doc, 12, 1],
        [:term, 13, 1, 0],
        [:term, 15, 1, 1],
        [:field, "columns"],
        [:int, 1],
        [:doc, 13, 4]
      ]
  end
end