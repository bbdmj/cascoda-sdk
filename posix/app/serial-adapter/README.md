<script type="text/javascript">
window.addEventListener('load', function() {
	let message = { height: document.body.scrollHeight, width: document.body.scrollWidth };	

	// window.top refers to parent window
	window.top.postMessage(message, "*");
});
</script>

# serial-adapter

`serial-adapter` is a useful program for interacting with an application running on baremetal. If a baremetal device (such as a Chili2) is running a serial application (such as ot-cli), then it can be controlled by running ``serial-adapter``, with no arguments, on a POSIX or Windows system.

The `serial-adapter` program, like all Cascoda host programs, uses the [ca821x-posix module](../../ca821x-posix/README.md), so is compatible with both UART and USB connected Chilis.
