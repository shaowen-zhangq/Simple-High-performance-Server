-- wrk Lua script to make requests and log status codes
request = function()
  return wrk.format(nil, nil, nil, nil)
end

response = function(status, headers, body)
  if status ~= 200 then
    io.stdout:write(string.format("status=%d\n", status))
  end
end
