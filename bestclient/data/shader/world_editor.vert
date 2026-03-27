out vec2 texCoord;

void main()
{
	vec2 aPos[3] = vec2[](
		vec2(-1.0, -1.0),
		vec2(3.0, -1.0),
		vec2(-1.0, 3.0)
	);
	vec2 Pos = aPos[gl_VertexID];
	gl_Position = vec4(Pos, 0.0, 1.0);
	texCoord = Pos * 0.5 + 0.5;
}
