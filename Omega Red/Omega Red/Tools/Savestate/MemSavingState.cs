﻿/*  Omega Red - Client PS2 Emulator for PCs
*
*  Omega Red is free software: you can redistribute it and/or modify it under the terms
*  of the GNU Lesser General Public License as published by the Free Software Found-
*  ation, either version 3 of the License, or (at your option) any later version.
*
*  Omega Red is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
*  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
*  PURPOSE.  See the GNU General Public License for more details.
*
*  You should have received a copy of the GNU General Public License along with Omega Red.
*  If not, see <http://www.gnu.org/licenses/>.
*/

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace Omega_Red.Tools.Savestate
{
    class MemSavingState : ISaveStateBase
    {
        BinaryWriter m_writer;

        public MemSavingState(BinaryWriter a_writer)
        {
            m_writer = a_writer;
        }

        public void FreezeTag(string a_tag)
        {
            if (m_writer == null)
                return;

            byte[] l_tagspace = new byte[32];

            var l_bytes = Encoding.ASCII.GetBytes(a_tag);

            l_bytes.CopyTo(l_tagspace, 0);

            m_writer.Write(l_tagspace);
        }

        public void Freeze(uint a_value)
        {
            if (m_writer == null)
                return;

            m_writer.Write(BitConverter.GetBytes(a_value));
        }

        public void Freeze(int a_value)
        {
            if (m_writer == null)
                return;

            m_writer.Write(BitConverter.GetBytes(a_value));
        }
                
        public void Freeze(byte[] a_value)
        {
            if (m_writer == null)
                return;

            m_writer.Write(a_value);
        }
    }
}
