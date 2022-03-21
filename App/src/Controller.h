#pragma once

#include <PlaystationCore/Controller.h>

#include <stdx/flat_unordered_map.h>

#include <SDL.h>

namespace App
{

struct ControllerBinding
{
	enum class InputType
	{
		Key,
		Button,
		Axis,
		AxisPos,
		AxisNeg
	};

	enum class OutputType
	{
		Button,
		Axis
	};

	InputType inputType;
	uint32_t input;

	OutputType outputType;
	uint32_t output;
};

class Controller
{
public:
	const auto& GetInputMap() const { return m_inputMap; }


private:
	std::vector<ControllerBinding> m_inputMap;
};

}