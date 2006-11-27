#!/usr/bin/env python
#
# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#
#<LicenseText>
#
# CitcomS.py by Eh Tan, Eun-seo Choi, and Pururav Thoutireddy.
# Copyright (C) 2002-2005, California Institute of Technology.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
#
#</LicenseText>
#
# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

'''Automatically find the input parameters from CitcomS input file and run
'batchcombine.py'

usage: autocombine.py machinefile inputfile step1 [step2 [...] ]
'''

# default values for CitcomS input
defaults = {'output_format': 'ascii',
            'nprocx': 1,
            'nprocy': 1,
            'nprocz': 1,
            'nodex': 9,
            'nodey': 9,
            'nodez': 9}

if __name__ == '__main__':

    import sys
    import batchcombine as bc

    if len(sys.argv) < 4:
        print __doc__
        sys.exit(1)

    machinefile = sys.argv[1]
    inputfile = sys.argv[2]
    timesteps = [int(i) for i in sys.argv[3:]]

    # parse input
    from parser import Parser
    parser = Parser(defaults)
    parser.read(inputfile)

    datadir = parser.getstr('datadir')
    datafile = parser.getstr('datafile')
    output_format = parser.getstr('output_format')

    if output_format != 'ascii':
        print "Error: don't know how to combine the output", \
              "(output_format=%s)" % output_format
        sys.exit(1)

    nodex = parser.getint('nodex')
    nodey = parser.getint('nodey')
    nodez = parser.getint('nodez')
    ncap = parser.getint('nproc_surf')
    nprocx = parser.getint('nprocx')
    nprocy = parser.getint('nprocy')
    nprocz = parser.getint('nprocz')

    totalnodes = nprocx * nprocy * nprocz * ncap
    nodelist = bc.machinefile2nodes(machinefile, totalnodes)

    for timestep in timesteps:
        bc.combine(nodelist, datadir, datafile, timestep,
                   nodex, nodey, nodez,
                   ncap, nprocx, nprocy, nprocz)



# version
# $Id$

# End of file
