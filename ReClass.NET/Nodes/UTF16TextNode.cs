﻿using System.Drawing;
using System.Text;
using ReClassNET.Memory;
using ReClassNET.UI;

namespace ReClassNET.Nodes
{
	public class Utf16TextNode : BaseTextNode
	{
		public override int CharacterSize => 2;

		public override Encoding Encoding => Encoding.Unicode;

		/// <summary>Draws this node.</summary>
		/// <param name="view">The view information.</param>
		/// <param name="x">The x coordinate.</param>
		/// <param name="y">The y coordinate.</param>
		/// <returns>The pixel size the node occupies.</returns>
		public override Size Draw(ViewInfo view, int x, int y)
		{
			return DrawText(view, x, y, "Text16", MemorySize / CharacterSize, view.Memory.ReadUtf16String(Offset, MemorySize));
		}

		public string ReadValueFromMemory(MemoryBuffer memory)
		{
			return memory.ReadUtf16String(Offset, MemorySize);
		}
	}
}
