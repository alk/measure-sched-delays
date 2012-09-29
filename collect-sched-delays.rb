#!/usr/bin/env ruby

def usage_and_exit
  puts "#{$0} [output-file]"
  exit 1
end

output_path = ARGV[0] || usage_and_exit

cpucount = Dir['/sys/devices/system/cpu/cpu[0-9]*'].length

path = File.dirname(__FILE__) + "/measure-sched-delays"

Signal.trap("HUP") {exit}
Signal.trap("INT") {exit}
Signal.trap("TERM") {exit}
Signal.trap("QUIT") {exit}

threads = (0...cpucount).map do |i|
  Thread.new do
    system "taskset -c #{i} #{path} >#{output_path}.cpu#{i}"
  end
end

threads.each {|t| t.join}
