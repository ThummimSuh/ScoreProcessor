// Trainer.cpp : Defines the entry point for the console application.
// This application will be the one used to train the machine learning algorithms.

#include "stdafx.h"
#include <iostream>
#include "../NeuralNetwork/neural_scaler.h"
#include <cstdlib>
#include <chrono>
#include <string>

using Img=cil::CImg<unsigned char>;
using net=neural_net::net<>;
using scaler=ScoreProcessor::neural_scaler;

void fill_in(float* const out,Img const& img,unsigned int const x,unsigned int const y,unsigned int const dim)
{
	auto idata=img._data+y*img._width;
	for(unsigned int yc=0;yc<dim;++yc)
	{
		auto const img_row=idata+yc*img._width;
		auto const data=out+yc*dim;
		for(unsigned int xc=0;xc<dim;++xc)
		{
			data[xc]=img_row[xc]/255.0f;
		}
	}
}
bool all_white(float const* d,size_t l)
{
	for(size_t i=0;i<l;++i)
	{
		if(d[i]!=1)
		{
			return false;
		}
	}
	return true;
}
void train(net& net,Img const& answer,Img const& input,unsigned int scale)
{
	auto int_square_root=[](auto n)
	{
		return static_cast<unsigned int>(std::round(std::sqrt(n)));
	};
	auto input_dim=int_square_root(net.layers().front().neuron_count());
	auto output_dim=int_square_root(net.layers().back().neuron_count());
	auto padding=(output_dim/scale-input_dim)/2;
	std::unique_ptr<float[]> ninput(new float[input_dim*input_dim]);
	std::unique_ptr<float[]> nanswer(new float[output_dim*output_dim]);
	for(unsigned int x=0;x+input_dim<input._width;++x)
	{
		for(unsigned int y=0;y+input_dim<input._height;++y)
		{
			fill_in(ninput.get(),input,x,y,input_dim);
			if(all_white(ninput.get(),input_dim*input_dim))
			{
				continue;
			}
			fill_in(nanswer.get(),answer,(x+padding)*scale,(y+padding)*scale,output_dim);
			net.train(ninput.get(),nanswer.get(),1);
		}
	}
}
bool has_comma(char const* str)
{
	while(str)
	{
		if(*str==',') return true;
		++str;
	}
	return false;
}
std::vector<unsigned int> parse_csv(char const* str)
{
	std::vector<unsigned int> ret;
	auto& err=errno;
	while(1)
	{
		char* end;
		err=0;
		auto val=std::strtoul(str,&end,10);
		if(err)
		{
			throw std::invalid_argument("Invalid values");
		}
		ret.push_back(val);
		if(*end=='\0')
		{
			ret.front()*=ret.front();
			ret.back()*=ret.back();
			return ret;
		}
		if(*end!=',')
		{
			throw std::invalid_argument("Invalid input");
		}
		str=end+1;
	}
}
int main(int argc,char** argv)
{
	if(argc<3)
	{
		std::cout<<
			__DATE__ " " __TIME__ "\n"
			"First arg is answer file.\n"
			"Second arg is input file.\n"
			"Third arg is CSV of layer heights (first and last values are squared) OR\n"
			"Third arg is name of file to base training on\n"
			"Fourth arg is name to save training file as, def %timestamp%.ssn\n";
		return 0;
	}
	try
	{
		cil::CImg<unsigned char> answer(argv[1]),input(argv[2]);
		unsigned int scale_factor;
		if(answer._height<=input._width||answer._height%input._height||answer._width%input._width||(scale_factor=answer._height/input._height)!=answer._width/input._width)
		{
			std::cout<<"Answer dimensions must be an (Natural + 2) multiple of the input dimensions\n";
			return 0;
		}
		auto my_scaler=has_comma(argv[3])?
			scaler(scale_factor,std::move(net(parse_csv(argv[3])).randomize())):
			scaler(argv[3]);
		if(my_scaler.scale_factor()!=scale_factor)
		{
			std::cout<<"Image scale does not fit scale factor of given file.\n";
			return 0;
		}
		train(my_scaler.net(),answer,input,scale_factor);
		if(argc>4)
		{
			my_scaler.save(argv[4]);
		}
		else
		{
			auto name=std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
			name.append(".ssn");
			my_scaler.save(name.c_str());
		}
	}
	catch(std::exception const& err)
	{
		std::cout<<err.what()<<'\n';
		return 1;
	}
	return 0;
}

