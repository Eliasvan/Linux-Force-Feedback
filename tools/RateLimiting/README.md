## Tools to test Rate-limiting

The following tools test the force-feedback driver's rate-limiting capabilities.

If no rate-limiting is used,
the application can be able to send commands to the device at a higher rate than the device can handle.
This can result in a buffer/queue building up,
and will be perceived as lag to the user.
In some situations,
it is practically equivalent with a denial of service.

If rate-limiting is supported by the driver,
no application should be able to cause previously mentioned problems.

To illustrate the problem (+ a potential solution),
check out this video:
https://www.youtube.com/watch?v=JG5HUPLuS1s


#### ffchoke

Extensive testing tool.
Compile, and get instructions with:

	gcc ffchoke.c -o ffchoke
	./ffchoke --help

#### fftest_buffer_overrun

Minimal testing tool.
Compile, and get instructions with:

	gcc fftest_buffer_overrun.c -o fftest_buffer_overrun
	./fftest_buffer_overrun --help
