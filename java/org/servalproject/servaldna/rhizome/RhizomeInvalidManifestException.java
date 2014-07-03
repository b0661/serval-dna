/**
 * Copyright (C) 2014 Serval Project Inc.
 *
 * This file is part of Serval Software (http://www.servalproject.org)
 *
 * Serval Software is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This source code is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this source code; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

package org.servalproject.servaldna.rhizome;

import java.net.URL;

/**
 * Thrown when a Rhizome API method is passed an invalid manifest.  This is not an error within the
 * Serval DNA interface, so it is not a subclass of ServalDInterfaceException.  The programmer must
 * explicitly deal with it instead of just absorbing it as an interface malfunction.
 *
 * @author Andrew Bettison <andrew@servalproject.com>
 */
public class RhizomeInvalidManifestException extends RhizomeException
{
	public RhizomeInvalidManifestException(URL url) {
		super("invalid manifest", url);
	}

}
