local dap = require 'dap'
dap.adapters.codelldb = {
	type = 'server',
	port = "13000",
	executable = {
		command = 'codelldb',
		args = { "--port", "13000" },
		-- On windows you may have to uncomment this:
		-- detached = false,
	}
}

dap.configurations.cpp = {
	{
		name = "skparagraph_demo",
		type = "codelldb",
		request = "launch",
		program = "build/main",
		cwd = '${workspaceFolder}',
		stopOnEntry = false,
		args = {}
	}
}
