#pragma once

#include <GLFW/glfw3.h>

// Scissor clipping still limits the rectangular bounds. Stencil depth lets
// rounded surfaces nest without repainting corners in an assumed parent color.
namespace Rendering {
inline int roundedClipDepth = 0; // Rendering runs on the UI thread.

template<typename DrawMask, typename Draw>
void withRoundedClip(int w, int h, float radius, DrawMask drawMask, Draw draw) {
	if (w <= 0 || h <= 0) return;
	if (radius <= 0.0f) {
		draw();
		return;
	}
	glPushAttrib(GL_STENCIL_BUFFER_BIT | GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_ENABLE_BIT | GL_CURRENT_BIT);
	GLboolean colorMask[4];
	glGetBooleanv(GL_COLOR_WRITEMASK, colorMask);
	const GLboolean depthTest = glIsEnabled(GL_DEPTH_TEST);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_ALPHA_TEST);
	glEnable(GL_STENCIL_TEST);
	glStencilMask(0xff);
	glStencilFunc(GL_EQUAL, roundedClipDepth, 0xff);
	glStencilOp(GL_KEEP, GL_KEEP, GL_INCR);
	glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
	drawMask();

	++roundedClipDepth;
	glColorMask(colorMask[0], colorMask[1], colorMask[2], colorMask[3]);
	if (depthTest) glEnable(GL_DEPTH_TEST);
	glStencilMask(0);
	glStencilFunc(GL_EQUAL, roundedClipDepth, 0xff);
	glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
	draw();

	// Remove only this level, preserving an enclosing rounded clip.
	glDisable(GL_DEPTH_TEST);
	glStencilMask(0xff);
	glStencilFunc(GL_EQUAL, roundedClipDepth, 0xff);
	glStencilOp(GL_KEEP, GL_KEEP, GL_DECR);
	glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
	drawMask();
	--roundedClipDepth;
	glPopAttrib();
}
}
