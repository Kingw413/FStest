## -*- Mode: python; py-indent-offset: 4; indent-tabs-mode: nil; coding: utf-8; -*-
'''

When using this tool, the wscript will look like:

	def options(opt):
	        opt.tool_options('ns3')

	def configure(conf):
		conf.load('compiler_cxx ns3')

                conf.check_ns3_modules()

	def build(bld):
		bld(source='main.cpp', target='app', use='CCNX')

Options are generated, in order to specify the location of ccnx includes/libraries.


'''

import waflib
from waflib.Configure import conf
from waflib import Utils,Logs,Errors

@conf
def _check_custom_dependencies(conf, modules):
    # Logs.pprint ('CYAN', '  + %s' % modules)
    found = []

    if not isinstance(modules, list):
        modules = Utils.to_list (modules)

    for module in modules:
        retval = conf.check_cfg(package = '%s' % (module), 
                                args='--cflags --libs',
                                msg="Checking for lib%s" % module,
                                uselib_store='CUSTOM_%s' % module.upper())

        if not retval is None:
            found.append(module)
    import copy
    if not 'CUSTOM_LIB_FOUND' in conf.env:
        conf.env['CUSTOM_LIB_FOUND'] = []
    conf.env['CUSTOM_LIB_FOUND'] = conf.env['CUSTOM_LIB_FOUND'] + copy.copy(found)

@conf
def check_custom_modules(conf, modules):

    if not 'CUSTOM_LIB_CHECK_MODULE_ONCE' in conf.env:
        conf.env['CUSTOM_LIB_CHECK_MODULE_ONCE'] = ''

        conf.check_cfg(atleast_pkgconfig_version='0.0.0')

    conf._check_custom_dependencies(modules)

