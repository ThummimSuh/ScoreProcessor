/*
Copyright(C) 2017-2018 Edward Xie

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program. If not, see <https://www.gnu.org/licenses/>.
*/
#include "stdafx.h"
#include <vector>
#include <memory>
#include <iostream>
#include <string>
//#define cimg_use_jpeg
#include "allAlgorithms.h"
#include "lib/threadpool/ThreadPool.h"
#include "lib/exstring/exfiles.h"
#include "shorthand.h"
#include "ImageProcess.h"
#include <algorithm>
#include "support.h"
#include <unordered_map>
#include <optional>
#include <array>
#include <numeric>
#include <sstream>
#include "parse.h"
using namespace cimg_library;
using namespace std;
using namespace ScoreProcessor;
using namespace ImageUtils;

class ChangeToGrayscale:public ImageProcess<> {
public:
	void process(Img& image)
	{
		if(image._spectrum>=3)
		{
			image=get_grayscale_simple(image);
		}
		else if(image._spectrum==1)
		{
			return;
		}
		else
		{
			throw std::invalid_argument("Invalid image spectrum");
		}
	}
};
class RemoveTransparency:public ImageProcess<> {
public:
	void process(Img& img)
	{
		if(img._spectrum==4)
		{
			remove_transparency(img,150,ColorRGB::WHITE);
		}
	}
};
class RemoveBorderGray:public ImageProcess<> {
	float tolerance;
public:
	RemoveBorderGray(float tolerance):tolerance(tolerance)
	{}
	void process(Img& image)
	{
		if(image._spectrum==1)
		{
			remove_border(image,0,tolerance);
		}
		else
		{
			throw std::invalid_argument("Remove process requires Grayscale image");
		}
	}
};
class FilterGray:public ImageProcess<> {
	unsigned char min;
	unsigned char max;
	Grayscale replacer;
public:
	FilterGray(unsigned char min,unsigned char max,Grayscale replacer):min(min),max(max),replacer(replacer)
	{}
	void process(Img& image)
	{
		if(image._spectrum==1)
		{
			replace_range(image,min,max,replacer);
		}
		else
		{
			throw std::invalid_argument("Filter Gray requires grayscale image");
		}
	}
};
class PadHoriz:public ImageProcess<> {
	unsigned int left,right;
public:
	PadHoriz(unsigned int const left,unsigned int const right):left(left),right(right)
	{}
	void process(Img& image)
	{
		horiz_padding(image,left,right);
	}
};
class PadVert:public ImageProcess<> {
	unsigned int top,bottom;
public:
	PadVert(unsigned int const top,unsigned int const bottom):top(top),bottom(bottom)
	{}
	void process(Img& img)
	{
		vert_padding(img,top,bottom);
	}
};
class PadAuto:public ImageProcess<> {
	unsigned int vert,min_h,max_h;
	signed int hoff;
	float opt_rat;
public:
	PadAuto(unsigned int vert,unsigned int min_h,unsigned int max_h,signed int hoff,float opt_rat)
		:vert(vert),min_h(min_h),max_h(max_h),hoff(hoff),opt_rat(opt_rat)
	{}
	void process(Img& img)
	{
		auto_padding(img,vert,max_h,min_h,hoff,opt_rat);
	}
};
class Rescale:public ImageProcess<> {
	double val;
	int interpolation;
public:
	enum rescale_mode {
		automatic=-2,
		raw_mem,
		boundary_fill,
		nearest_neighbor,
		moving_average,
		linear,
		grid,
		cubic,
		lanczos
	};
	Rescale(double val,int interpolation):
		val(val),
		interpolation(interpolation==automatic?(val>1?cubic:moving_average):interpolation)
	{}
	void process(Img& img)
	{
		img.resize(
			scast<int>(std::round(img._width*val)),
			scast<int>(std::round(img._height*val)),
			img._depth,
			img._spectrum,
			interpolation);
	}
};
class ClusterClearGray:public ImageProcess<> {
	unsigned int min,max;
	Grayscale background;
	float tolerance;
public:
	ClusterClearGray(unsigned int min,unsigned int max,Grayscale background,float tolerance):min(min),max(max),background(background),tolerance(tolerance)
	{}
	void process(Img& img) override
	{
		if(img._spectrum==1)
		{
			clear_clusters(img,rcast<ucharcp>(&background),
				ImageUtils::Grayscale::color_diff,tolerance,true,min,max,rcast<ucharcp>(&background));
		}
		else
		{
			throw std::invalid_argument("Cluster Clear Grayscale requires grayscale image");
		}
	}
};
class ClusterClearGrayAlt:public ImageProcess<> {
	unsigned char required_min,required_max;
	unsigned int min_size,max_size;
	unsigned char sel_min;
	unsigned char sel_max;
	unsigned char background;
public:
	ClusterClearGrayAlt(unsigned char rcmi,unsigned char rcma,unsigned int mis,unsigned mas,unsigned char smi,unsigned char sma,unsigned char back):
		required_min(rcmi),required_max(rcma),min_size(mis),max_size(mas),sel_min(smi),sel_max(sma),background(back)
	{}
	void process(Img& img)
	{
		if(img._spectrum==1)
		{
			clear_clusters(img,background,
				[this](unsigned char v)
			{
				return v>=sel_min&&v<=sel_max;
			},
				[this,&img](Cluster const& c)
			{
				auto size=c.size();
				if(size>=min_size&&size<=max_size)
				{
					return true;
				}
				for(auto const& rect:c.get_ranges())
				{
					for(unsigned int y=rect.top;y<rect.bottom;++y)
					{
						auto row=img._data+y*img._width;
						for(unsigned int x=rect.left;x<rect.right;++x)
						{
							auto pix=*(row+x);
							if(pix>=required_min&&pix<=required_max)
							{
								return false;
							}
						}
					}
				}
				return true;
			});
		}
		else
		{
			throw std::invalid_argument("Cluster Clear Gray requires grayscale image");
		}
	}
};
class RescaleGray:public ImageProcess<> {
	unsigned char min,mid,max;
public:
	RescaleGray(unsigned char min,unsigned char mid,unsigned char max=255):min(min),mid(mid),max(max)
	{}
	void process(Img& img)
	{
		if(img._spectrum==1)
		{
			rescale_colors(img,min,mid,max);
		}
		else
		{
			throw std::invalid_argument("Rescale Gray requires grayscale image");
		}
	}
};
class FillSelectionAbsGray:public ImageProcess<> {
	ImageUtils::Rectangle<unsigned int> rect;
	Grayscale color;
public:
	FillSelectionAbsGray(ImageUtils::Rectangle<unsigned int> rect,Grayscale g):rect(rect),color(g)
	{}
	void process(Img& img) override
	{
		auto temp=rect;
		if(temp.right>img._width)
		{
			temp.right=img._width;
		}
		if(temp.bottom>img._height)
		{
			temp.bottom=img._height;
		}
		fill_selection(img,temp,color);
	}
};
class Blur:public ImageProcess<> {
	float radius;
public:
	Blur(float radius):radius(radius)
	{}
	void process(Img& img) override
	{
		img.blur(radius);
	}
};
class Straighten:public ImageProcess<> {
	double pixel_prec;
	unsigned int num_steps;
	double min_angle,max_angle;
	unsigned char boundary;
public:
	Straighten(double pixel_prec,double min_angle,double max_angle,double angle_prec,unsigned char boundary)
		:pixel_prec(pixel_prec),
		min_angle(M_PI_2+min_angle*DEG_RAD),max_angle(M_PI_2+max_angle*DEG_RAD),
		num_steps(std::ceil((max_angle-min_angle)/angle_prec)),
		boundary(boundary)
	{}
	void process(Img& img) override
	{
		auto_rotate_bare(img,pixel_prec,min_angle,max_angle,num_steps,boundary);
	}
};

class Rotate:public ImageProcess<> {
	float angle;
public:
	Rotate(float angle):angle(angle)
	{}
	void process(Img& img) override
	{
		img.rotate(angle,2,1);
	}
};

void stop()
{
	cout<<"Stopped\n";
	std::this_thread::sleep_for(std::chrono::seconds(1000));
}
class CoutLog:public Log {
	CoutLog()
	{}
	static CoutLog singleton;
public:
	static Log& get()
	{
		return singleton;
	}
	void log(char const* msg,size_t) override
	{
		std::cout<<msg;
	}
	void log_error(char const* msg,size_t) override
	{
		std::cout<<msg;
	}
};
CoutLog CoutLog::singleton;

class CommandMaker {
public:
	typedef std::vector<std::string>::iterator iter;
	struct delivery {
		SaveRules sr;
		unsigned int starting_index;
		bool do_move;
		ProcessList<unsigned char> pl;
		enum do_state {
			do_absolutely_nothing,
			do_nothing,
			do_single,
			do_cut,
			do_splice
		};
		do_state flag;
		struct {
			unsigned int horiz_padding;
			unsigned int optimal_padding;
			unsigned int min_padding;
			unsigned int optimal_height;
			float excess_weight;
			float padding_weight;
		} splice_args;
		std::regex rgx;
		enum regex_state {
			unassigned,normal,inverted
		};
		regex_state rgxst;
		unsigned int num_threads;
		bool list_files;
		delivery():starting_index(1),flag(do_absolutely_nothing),num_threads(0),rgxst(unassigned),do_move(false),list_files(false)
		{}
	};
private:
	unsigned int min_args;
	unsigned int max_args;
	char const* _help_message;
	char const* _name;
protected:
	CommandMaker(unsigned int min_args,unsigned int max_args,char const* hm,char const* nm)
		:min_args(min_args),max_args(max_args),_help_message(hm),_name(nm)
	{}
	virtual char const* parse_command(iter begin,size_t num_args,delivery&) const=0;
public:
	virtual ~CommandMaker()=default;
	CommandMaker(CommandMaker const&)=delete;
	CommandMaker(CommandMaker&&)=delete;
	char const* help_message() const
	{
		return _help_message;
	}
	char const* name() const
	{
		return _name;
	}
	char const* make_command(iter begin,iter end,delivery& del) const
	{
		size_t n=std::distance(begin,end);
		if(n<min_args)
		{
			return "Too few parameters";
		}
		if(n>max_args)
		{
			return "Too many parameters";
		}
		return parse_command(begin,n,del);
	}
};

class SingleCommandMaker:public CommandMaker {
protected:
	virtual char const* parse_command_h(iter begin,size_t num_args,delivery&) const=0;
	SingleCommandMaker(unsigned int min_args,unsigned int max_args,char const* hm,char const* nm)
		:CommandMaker(min_args,max_args,hm,nm)
	{}
	static char const* const mci;
	char const* parse_command(iter begin,size_t num_args,delivery& del) const override final
	{
		if(del.flag>del.do_single)
		{
			return mci;
		}
		del.flag=del.do_single;
		return parse_command_h(begin,num_args,del);
	}
};
char const* const SingleCommandMaker::mci="Single Command cannot be done with a Multi Command";

class FilterGrayMaker:public SingleCommandMaker {
	static char const* assign_val(iter arg,int* hold)
	{
		try
		{
			*hold=std::stoi(*arg);
			if(*hold<0||*hold>255)
			{
				return "Values must be in range [0,255]";
			}
		}
		catch(std::exception const&)
		{
			return "Invalid parameter given";
		}
		return nullptr;
	}
	FilterGrayMaker():SingleCommandMaker(1,3,"Replaces all values between min and max value inclusive with replacer","Filter Gray")
	{}
	static FilterGrayMaker const singleton;
public:
	static CommandMaker const& get()
	{
		return singleton;
	}
protected:
	char const* parse_command_h(iter argb,size_t n,delivery& del) const override
	{
		int params[3];
		size_t i;
		for(i=0;i<n;++i)
		{
			if(auto res=assign_val(argb+i,params+i))
			{
				return res;
			}
		}
		for(;i<3;++i)
		{
			params[i]=255;
		}
		del.pl.add_process<FilterGray>(params[0],params[1],Grayscale(params[2]));
		return nullptr;
	}
};
FilterGrayMaker const FilterGrayMaker::singleton;

class ConvertGrayMaker:public SingleCommandMaker {
	ConvertGrayMaker():SingleCommandMaker(0,0,"Converts given image to Grayscale","Convert Gray")
	{}
	static ConvertGrayMaker const singleton;
public:
	static CommandMaker const& get()
	{
		return singleton;
	}
protected:
	char const* parse_command_h(iter begin,size_t n,delivery& del) const override
	{
		del.pl.add_process<ChangeToGrayscale>();
		return nullptr;
	}
};
ConvertGrayMaker const ConvertGrayMaker::singleton;

class ClusterClearMaker:public SingleCommandMaker {
	ClusterClearMaker()
		:SingleCommandMaker(1,4,
			"All clusters of pixels that are outside of tolerance of background color\n"
			"and between min and max size are replaced by the background color",
			"Cluster Clear Grayscale")
	{}
	static ClusterClearMaker const singleton;
public:
	static CommandMaker const& get()
	{
		return singleton;
	}
protected:
	char const* parse_command_h(iter begin,size_t n,delivery& del) const override
	{
		int max_size,min_size;
		Grayscale background;
		float tolerance;
		try
		{
			max_size=std::stoi(begin[0]);
			if(max_size<0)
			{
				return "Maximum cluster size must be non-negative";
			}
		}
		catch(std::exception const&)
		{
			return "Invalid input for maximum size";
		}
		if(n<2)
		{
			goto def_min_size;
		}
		try
		{
			min_size=std::stoi(begin[1]);
			if(min_size<0)
			{
				return "Minimum cluster size must be non-negative";
			}
		}
		catch(std::exception const&)
		{
			return "Invalid input for minimum size";
		}
		if(max_size<min_size)
		{
			return "Max size must be greater than min size";
		}
		if(n<3)
		{
			goto def_background;
		}
		try
		{
			int bg=std::stoi(begin[2]);
			if(bg>255||bg<0)
			{
				return "Background color must be in range [0,255]";
			}
			background=bg;
		}
		catch(std::exception const&)
		{
			return "Invalid input for background color";
		}
		if(n<4)
		{
			goto def_tolerance;
		}
		try
		{
			tolerance=std::stof(begin[3]);
			if(tolerance<0||tolerance>1)
			{
				return "Tolerance must be between 0 and 1";
			}
		}
		catch(std::exception const&)
		{
			return "Invalid input for tolerance";
		}
	end:
		del.pl.add_process<ClusterClearGray>(min_size,max_size,background,tolerance);
		return nullptr;
	def_min_size:
		min_size=0;
	def_background:
		background=255;
	def_tolerance:
		tolerance=0.042f;
		goto end;
	}
};
ClusterClearMaker const ClusterClearMaker::singleton;

class ClusterClearAltMaker:public SingleCommandMaker {
	ClusterClearAltMaker()
		:SingleCommandMaker(2,4,
			"Pixels are selected that are in sel_range inclusive and clustered.\n"
			"Clusters that are in the bad_size_range inclusive\n"
			"or which do not contain a color in required_color_range inclusive\n"
			"are replaced by the background color",
			"Cluster Clear Grayscale")
	{}
	static ClusterClearAltMaker const singleton;
public:
	static CommandMaker const& get()
	{
		return singleton;
	}
protected:
	char const* parse_command_h(iter begin,size_t n,delivery& del) const override
	{
		static char const* const rcr_errors[]={
			"Range not given for required_color_range",
			"Too many arguments for required_color_range",
			"Should not have happened",
			"Missing argument for upper bound of required_color_range",
			"Invalid argument for lower bound of required_color_range",
			"Invalid argument for upper bound of required_color_range",
			"Lower bound cannot be greater than upper bound of required_color_range"
		};
		std::array<unsigned char,2> required_color_range;
		constexpr std::array<std::optional<unsigned char>,2> const rcr_def={std::optional<unsigned char>(0),std::optional<unsigned char>()};
		auto ordered=[](std::array<unsigned char,2> const& arr)
		{
			if(arr[0]>arr[1])
			{
				return 6;
			}
			return -1;
		};
		auto res=parse_range(
			required_color_range,
			begin[0],
			rcr_def,
			ordered);
		if(res!=-1)
		{
			return rcr_errors[res];
		}
		static char const* const bsr_errors[]={
			"Range not given for bad_size_range",
			"Too many arguments for bad_size_range",
			"Should not have happened",
			"Missing argument for upper bound of bad_size_range",
			"Invalid argument for lower bound of bad_size_range",
			"Invalid argument for upper bound of bad_size_range",
			"Lower bound cannot be greater than upper bound of bad_size_range"
		};
		constexpr std::array<std::optional<unsigned int>,2> const bsr_def={std::optional<unsigned int>(0),std::optional<unsigned int>()};
		std::array<unsigned int,2> bad_size_range;
		auto bres=parse_range(bad_size_range,begin[1],bsr_def,[](decltype(bad_size_range) const& arr)
		{
			if(arr[0]>arr[1])
			{
				return 6;
			}
			return -1;
		});
		if(bres!=-1)
		{
			return bsr_errors[bres];
		}
		std::array<unsigned char,2> sel_range;
		if(n>2)
		{
			static char const* const sr_errors[]={
				"Range not given for sel_range",
				"Too many arguments for sel_range",
				"Should not have happened",
				"Should not have happened",
				"Invalid argument for lower bound of sel_range",
				"Invalid argument for upper bound of sel_range",
				"Lower bound cannot be greater than upper bound of sel_range"
			};
			std::array<std::optional<unsigned char>,2> sr_def={std::optional<unsigned char>(0),std::optional<unsigned char>(200)};
			auto sres=parse_range(sel_range,begin[2],sr_def,ordered);
			if(sres!=-1)
			{
				return sr_errors[sres];
			}
		}
		else
		{
			goto init_sel_range;
		}
		unsigned char background;
		if(n>3)
		{
			auto bres=parse_str(background,begin[3].c_str());
			if(bres)
			{
				return "Invalid argument for background";
			}
		}
		else
		{
			goto init_background;
		}
	end:
		del.pl.add_process<ClusterClearGrayAlt>(
			required_color_range[0],required_color_range[1],
			bad_size_range[0],bad_size_range[1],
			sel_range[0],sel_range[1],
			background);
		return nullptr;
	init_sel_range:
		sel_range[0]=0;
		sel_range[1]=200;
	init_background:
		background=255;
		goto end;
	}
};
ClusterClearAltMaker const ClusterClearAltMaker::singleton;

class HorizontalPaddingMaker:public SingleCommandMaker {
	HorizontalPaddingMaker():
		SingleCommandMaker(1,2,"Pads the left and right sides of the image with given number of pixels","Horizontal Padding")
	{}
	static HorizontalPaddingMaker const singleton;
public:
	static CommandMaker const& get()
	{
		return singleton;
	}
protected:
	char const* parse_command_h(iter begin,size_t n,delivery& del) const override
	{
		try
		{
			int amount=std::stoi(*begin);
			if(amount<0)
			{
				return "Padding must be non-negative";
			}
			if(n>1)
			{
				int a=std::stoi(begin[1]);
				if(a<0)
				{
					return "Padding must be non-negative";
				}
				del.pl.add_process<PadHoriz>(amount,a);
			}
			else
			{
				del.pl.add_process<PadHoriz>(amount,amount);
			}
			return nullptr;
		}
		catch(std::exception const&)
		{
			return "Invalid input";
		}
	}
};
HorizontalPaddingMaker const HorizontalPaddingMaker::singleton;

class VerticalPaddingMaker:public SingleCommandMaker {
	VerticalPaddingMaker()
		:SingleCommandMaker(1,2,"Pads the top and bottom of the image with given number of pixels","Vertical Padding")
	{}
	static VerticalPaddingMaker const singleton;
public:
	static CommandMaker const& get()
	{
		return singleton;
	}
protected:
	char const* parse_command_h(iter begin,size_t n,delivery& del) const override
	{
		try
		{
			int amount=std::stoi(*begin);
			if(amount<0)
			{
				return "Padding must be non-negative";
			}
			if(n>1)
			{
				int a=std::stoi(begin[1]);
				if(a<0)
				{
					return "Padding must be non-negative";
				}
				del.pl.add_process<PadVert>(amount,a);
			}
			else
			{
				del.pl.add_process<PadVert>(amount,amount);
			}
			return nullptr;
		}
		catch(std::exception const&)
		{
			return "Invalid input";
		}
	}
};
VerticalPaddingMaker const VerticalPaddingMaker::singleton;

class OutputMaker:public CommandMaker {
	OutputMaker():
		CommandMaker(
			1,2,
			"Pattern templates:\n"
			"  %c copy whole filename (includes path)\n"
			"  %p copy path (does not include trailing slash)\n"
			"  %x copy extension (does not include dot)\n"
			"  %f copy filename (does not include path, dot, or extension)\n"
			"  %w copy whole name (filename with extension)\n"
			"  %0 any number from 0-9, index of file with specified number of padding\n"
			"  %% literal percent\n"
			"Anything else will be interpreted as a literal character\n"
			"Pattern is %w if no output is specified\n"
			"If you want the file to start with -, prepend an additional -\n"
			"  e.g.: \"-my-file.txt\" -> \"--my-file.txt\"\n"
			"This is only done for starting dashes\n"
			"Move is whether file should be moved or copied (ignored for multi)",
			"Output Pattern")
	{}
	static OutputMaker const singleton;
public:
	static CommandMaker const& get()
	{
		return singleton;
	}
protected:
	char const* parse_command(iter begin,size_t n,delivery& del) const override
	{
		if(begin->empty())
		{
			return "Output format cannot be empty";
		}
		if(del.sr.empty())
		{
			try
			{
				if((*begin).front()=='-')
				{
					del.sr.assign((*begin).c_str()+1);
				}
				else
				{
					del.sr.assign(*begin);
				}
			}
			catch(std::exception const&)
			{
				return "Invalid template";
			}
			if(n>1)
			{
				if(begin[1][0]=='0'||begin[1][0]=='f')
				{
					del.do_move=false;
				}
				else
				{
					del.do_move=true;
				}
			}
			else
			{
				del.do_move=false;
			}
		}
		else
		{
			return "Filename template already given";
		}
		if(del.flag<del.do_nothing)
		{
			del.flag=del.do_nothing;
		}
		return nullptr;
	}
};
OutputMaker const OutputMaker::singleton;

class StartingIndexMaker:public CommandMaker {
	StartingIndexMaker():CommandMaker(1,1,"The starting index to label template names and spliced pages","Starting Index")
	{}
	static StartingIndexMaker const singleton;
public:
	static CommandMaker const& get()
	{
		return singleton;
	}
	char const* parse_command(iter begin,size_t n,delivery& del) const override
	{
		unsigned int num;
		auto err=ScoreProcessor::parse_str(num,begin[0].c_str());
		if(err)
		{
			return "Invalid starting index input";
		}
		del.starting_index=num;
		return nullptr;
	}
};
StartingIndexMaker const StartingIndexMaker::singleton;

class ListFilesMaker:public CommandMaker {
	ListFilesMaker():CommandMaker(0,0,"Makes program list out files","List Files")
	{}
	static ListFilesMaker const singleton;
public:
	static CommandMaker const& get()
	{
		return singleton;
	}
	char const* parse_command(iter,size_t,delivery& del) const override
	{
		del.list_files=true;
		return nullptr;
	}
};
ListFilesMaker const ListFilesMaker::singleton;

class AutoPaddingMaker:public SingleCommandMaker {
	AutoPaddingMaker()
		:SingleCommandMaker(
			3,5,
			"Attempts to make the image fit the desired ratio.\n"
			"Top and bottom are padded by vertical padding.\n"
			"Left is padded by somewhere between min padding and max padding.\n"
			"Right is padded by left padding plus horizontal offset.",
			"Auto Padding")
	{}
	static AutoPaddingMaker const singleton;
public:
	static CommandMaker const& get()
	{
		return singleton;
	}
protected:
	char const* parse_command_h(iter begin,size_t n,delivery& del) const override
	{
		int vert,minh,maxh,hoff;
		float opt_rat;
		try
		{
			vert=std::stoul(begin[0]);
			if(vert<0)
			{
				return "Vertical padding must be non-negative";
			}
		}
		catch(std::exception const&)
		{
			return "Invalid argument given for vertical padding";
		}
		try
		{
			minh=std::stoi(begin[1]);
			if(minh<0)
			{
				return "Minimum horizontal padding must be non-negative";
			}
		}
		catch(std::exception const&)
		{
			return "Invalid input for minimum horizontal padding";
		}
		try
		{
			maxh=std::stoi(begin[2]);
			if(maxh<0)
			{
				return "Maximum horizontal padding must be non-negative";
			}
			if(maxh<minh)
			{
				return "Maximum horizontal padding must be greater than minimum";
			}
		}
		catch(std::exception const&)
		{
			return "Invalid argument given for max horizontal padding";
		}
		if(n>3)
		{
			try
			{
				hoff=std::stoi(begin[3]);
				if(hoff<0&&-hoff>minh)
				{
					return "Negative horizontal offset must be less than minimum horizontal padding";
				}
			}
			catch(std::exception const&)
			{
				return "Invalid argument given for horizontal offset";
			}
		}
		else
		{
			hoff=0;
			goto end;
		}
		if(n>4)
		{
			try
			{
				opt_rat=std::stof(begin[4]);
				if(opt_rat<0)
				{
					return "Optimal ratio must be non-negative";
				}
			}
			catch(std::exception const&)
			{
				return "Invalid argument given for optimal ratio";
			}
		}
		else
		{
			opt_rat=16.0f/9.0f;
		}
	end:
		del.pl.add_process<PadAuto>(vert,minh,maxh,hoff,opt_rat);
		return nullptr;
	}
};
AutoPaddingMaker const AutoPaddingMaker::singleton;

class RescaleGrayMaker:public SingleCommandMaker {
	RescaleGrayMaker()
		:SingleCommandMaker(3,3,
			"Colors are scaled such that values less than or equal to min become 0,\n"
			"and values greater than or equal to max becomes 255.\n"
			"They are scaled based on their distance from mid.",
			"Rescale Gray")
	{}
	static RescaleGrayMaker const singleton;
	static char const* const errors[3];
public:
	static CommandMaker const& get()
	{
		return singleton;
	}
protected:
	char const* parse_command_h(iter begin,size_t,delivery& del) const override
	{
		int params[3];
		for(size_t i=0;i<3;++i)
		{
			try
			{
				params[i]=std::stoi(begin[i]);
				if(params[i]<0||params[i]>255)
				{
					return "Values must be in range [0,255]";
				}
			}
			catch(std::exception const&)
			{
				return errors[i];
			}
		}
		if(params[0]>params[1])
		{
			return "Min value must be less than or equal to mid value";
		}
		if(params[1]>params[2])
		{
			return "Mid value must be less than or equal to max value";
		}
		del.pl.add_process<RescaleGray>(params[0],params[1],params[2]);
		return nullptr;
	}
};
RescaleGrayMaker const RescaleGrayMaker::singleton;
char const* const RescaleGrayMaker::errors[3]={"Invalid argument for min value","Invalid argument for mid value","Invalid argument for max value"};

class CutMaker:public CommandMaker {
	CutMaker()
		:CommandMaker(0,0,"Cuts the image into separate systems","Cut")
	{}
	static CutMaker const singleton;
public:
	static CommandMaker const& get()
	{
		return singleton;
	}
protected:
	char const* parse_command(iter begin,size_t,delivery& del) const override
	{
		if(del.flag>del.do_nothing)
		{
			return "Cut can not be done along with other commands";
		}
		del.flag=delivery::do_cut;
		return nullptr;
	}
};
CutMaker const CutMaker::singleton;

class SpliceMaker:public CommandMaker {
	SpliceMaker():
		CommandMaker(
			3,6,
			"Splices the pages together assuming right alignment.\n"
			"Knuth algorithm that tries to minimize deviation from optimal height and optimal padding.\n"
			"Horizontal padding is the padding placed between elements of the page horizontally.\n"
			"Min padding is the minimal vertical padding between pages.\n"
			"Excess weight is the penalty weight applied to height deviation above optimal\n"
			"Pad weight is weight of pad deviation relative to height deviation\n"
			"Cost function is\n"
			"  if(height>opt_height)\n"
			"    (excess_weight*(height-opt_height)/opt_height)^3+\n"
			"    (pad_weight*abs_dif(padding,opt_padding)/opt_padding)^3\n"
			"  else\n"
			"    ((opt_height-height)/opt_height)^3+\n"
			"    (pad_weight*abs_dif(padding,opt_padding)/opt_padding)^3",
			"Splice")
	{}
	static SpliceMaker const singleton;
public:
	static CommandMaker const& get()
	{
		return singleton;
	}
protected:
	char const* parse_command(iter begin,size_t n,delivery& del) const override
	{
		if(del.flag>del.do_nothing)
		{
			return "Splice can not be done along with other commands";
		}
		del.flag=del.do_splice;
		int hpadding,opadding,mpadding,oheight=-1;
		float excess_weight=10;
		float padding_weight=1;
		try
		{
			hpadding=std::stoi(begin[0]);
			if(hpadding<0)
			{
				return "Horizontal padding must be non-negative";
			}
		}
		catch(std::exception const&)
		{
			return "Invalid input for horizontal padding";
		}
		try
		{
			opadding=std::stoi(begin[1]);
			if(opadding<0)
			{
				return "Optimal padding must be non-negative";
			}
		}
		catch(std::exception const&)
		{
			return "Invalid input for optimal padding";
		}
		try
		{
			mpadding=std::stoi(begin[2]);
			if(mpadding<0)
			{
				return "Minimum padding must be non-negative";
			}
		}
		catch(std::exception const&)
		{
			return "Invalid input for minimum padding";
		}
		if(n>3)
		{
			try
			{
				oheight=std::stoi(begin[3]);
				if(oheight<-1)
				{
					return "Optimal height must be non-negative";
				}
			}
			catch(std::exception const&)
			{
				return "Invalid input for optimal height";
			}
			if(n>4)
			{
				try
				{
					excess_weight=std::stof(begin[4]);
				}
				catch(std::exception const&)
				{
					return "Invald input for excess height penalty";
				}
				if(excess_weight<0)
				{
					return "Penalty for excess height must be positive";
				}
				if(n>5)
				{
					try
					{
						padding_weight=std::stof(begin[5]);
					}
					catch(std::exception const&)
					{
						return "Invalid input for padding weight";
					}
					if(padding_weight<0)
					{
						return "Padding weight must be positive";
					}
				}
			}
		}
		del.flag=delivery::do_splice;
		del.splice_args.horiz_padding=hpadding;
		del.splice_args.min_padding=mpadding;
		del.splice_args.optimal_padding=opadding;
		del.splice_args.optimal_height=oheight;
		del.splice_args.excess_weight=excess_weight;
		del.splice_args.padding_weight=padding_weight;
		return nullptr;
	}
};
SpliceMaker const SpliceMaker::singleton;

class BlurMaker:public SingleCommandMaker {
	BlurMaker()
		:SingleCommandMaker(1,1,"Gaussian blur of given standard deviation","Blur")
	{}
	static BlurMaker const singleton;
public:
	static CommandMaker const& get()
	{
		return singleton;
	}
protected:
	char const* parse_command_h(iter begin,size_t n,delivery& del) const override
	{
		float radius;
		try
		{
			radius=std::stof(*begin);
			if(radius<0)
			{
				return "Blur radius must be non-negative";
			}
		}
		catch(std::exception const&)
		{
			return "Invalid arguments given for blur";
		}
		del.pl.add_process<Blur>(radius);
		return nullptr;
	}
};
BlurMaker const BlurMaker::singleton;

class StraightenMaker:public SingleCommandMaker {
	StraightenMaker()
		:SingleCommandMaker(
			0,5,
			"Straightens the image\n"
			"Min angle is minimum angle of range to consider rotation (in degrees)\n"
			"Max angle is maximum angle of range to consider rotation (in degrees)\n"
			"Angle precision is precision in measuring angle (in degrees)\n"
			"Pixel precision is precision when measuring distance from origin\n"
			"A pixel is considered an edge if there is a vertical transition across boundary",
			"Straighten")
	{}
	static StraightenMaker const singleton;
public:
	static CommandMaker const& get()
	{
		return singleton;
	}
protected:
	char const* parse_command_h(iter begin,size_t n,delivery& del) const override
	{
		double pixel_prec,min_angle,max_angle,angle_prec;
		unsigned char boundary;
#define assign_val(val,index,cond,check_statement,error_statement)\
		if(n>##index##)\
		{\
			try\
			{\
				##val##=std::stod(begin[##index##]);\
				if(##cond##)\
				{\
					return ##check_statement##;\
				}\
			}\
			catch(std::exception const&)\
			{\
				return "Invalid input for " ## error_statement ##;\
			}\
		}\
		else\
		{\
			goto init_##val##;\
		}
		assign_val(min_angle,0,false,"","minimum angle");
		assign_val(max_angle,1,false,"","maximum angle");
		assign_val(angle_prec,2,angle_prec<=0,"Angle precision must be positive","angle precision");
		assign_val(pixel_prec,3,pixel_prec<=0,"Pixel precision must be positive","pixel precision");
#undef assign_val
		;
		if(n>4)
		{
			auto res=parse_str(boundary,begin[4].c_str());
			if(res)
			{
				return "Invalid input for boundary";
			}
		}
		else
		{
			goto init_boundary;
		}
	end:
		if(min_angle>max_angle)
		{
			return "Max angle must be greater than min angle";
		}
		if(max_angle-min_angle>180)
		{
			return "Difference between angles must be less than or equal to 180";
		}
		del.pl.add_process<Straighten>(pixel_prec,min_angle,max_angle,angle_prec,boundary);
		return nullptr;
	init_min_angle:
		min_angle=-5.0;
	init_max_angle:
		max_angle=5.0;
	init_angle_prec:
		angle_prec=0.1;
	init_pixel_prec:
		pixel_prec=1.0;
	init_boundary:
		boundary=128;
		goto end;
	}
};
StraightenMaker const StraightenMaker::singleton;

class RemoveBorderMaker:public SingleCommandMaker {
	RemoveBorderMaker()
		:SingleCommandMaker(0,1,"Removes border of image (SUPER BETA VERSION)","Remove Border")
	{}
	static RemoveBorderMaker const singleton;
public:
	static CommandMaker const& get()
	{
		return singleton;
	}
protected:
	char const* parse_command_h(iter begin,size_t n,delivery& del) const override
	{
		float tolerance;
		if(n>0)
		{
			try
			{
				tolerance=std::stof(*begin);
				if(tolerance<0||tolerance>1)
				{
					return "Tolerance must be in range [0,1]";
				}
			}
			catch(std::exception const&)
			{
				return "Invalid input for tolerance";
			}
		}
		else
		{
			tolerance=0.5;
		}
		del.pl.add_process<RemoveBorderGray>(tolerance);
		return nullptr;
	}
};
RemoveBorderMaker const RemoveBorderMaker::singleton;

class RescaleMaker:public SingleCommandMaker {
	RescaleMaker():SingleCommandMaker(1,2,
		"Rescales image by given factor\n"
		"Rescale modes are:\n"
		"  auto (moving average if downscaling, else cubic)\n"
		"  nearest neighbor\n"
		"  moving average\n"
		"  linear\n"
		"  grid\n"
		"  cubic\n"
		"  lanczos\n"
		"To specify mode, type as many letters as needed to unambiguously identify mode",
		"Rescale")
	{}
	static RescaleMaker const singleton;
public:
	static CommandMaker const& get()
	{
		return singleton;
	}
protected:
	char const* parse_command_h(iter begin,size_t n,delivery& del) const override
	{
		try
		{
			double factor=std::stod(*begin);
			if(factor<0)
			{
				return "Rescale factor must be non-negative";
			}
			int mode=Rescale::automatic;
			if(n>1)
			{
				std::string const& mode_string=begin[1];
				switch(mode_string[0]) //thank you null-termination
				{
					case 'a':
						mode=Rescale::automatic;
						break;
					case 'n':
						mode=Rescale::nearest_neighbor;
						break;
					case 'm':
						mode=Rescale::moving_average;
						break;
					case 'l':
						switch(mode_string[1])
						{
							case 'i':
								mode=Rescale::linear;
								break;
							case 'a':
								mode=Rescale::lanczos;
								break;
							case '\0':
								return "Ambiguous mode starting with l";
								break;
							default:
								return "Mode does not exist";
						}
						break;
					case 'g':
						mode=Rescale::grid;
						break;
					case 'c':
						mode=Rescale::cubic;
						break;
					default:
						return "Mode does not exist";
				}
			}
			del.pl.add_process<Rescale>(factor,mode);
		}
		catch(std::exception const&)
		{
			return "Invalid input for rescale factor";
		}
		return nullptr;
	}
};
RescaleMaker const RescaleMaker::singleton;

class LogMaker:public CommandMaker {
	LogMaker()
		:CommandMaker(1,1,"Changes verbosity of output: Silent=0, Errors-only=1 (default), Loud=2","Verbosity")
	{}
	static LogMaker const singleton;
public:
	static CommandMaker const& get()
	{
		return singleton;
	}
protected:
	char const* parse_command(iter begin,size_t,delivery& del) const override
	{
		if(del.pl.get_log())
		{
			return "Verbosity level already given";
		}
		try
		{
			int lvl=std::stoi(*begin);
			if(lvl<0||lvl>2)
			{
				return "Invalid level";
			}
			del.pl.set_log(&CoutLog::get());
			del.pl.set_verbosity(decltype(del.pl)::verbosity(lvl));
			return nullptr;
		}
		catch(std::exception const&)
		{
			return "Invalid input for level";
		}
	}
};
LogMaker const LogMaker::singleton;

class RegexMaker:public CommandMaker {
	RegexMaker():
		CommandMaker(
			1,2,
			"Filters the folder of files using a regex pattern\n"
			"Files that match are kept, unless inversion option is specified as true\n"
			"Giving a string starting with 0 or f is false, otherwise is true\n"
			"Does nothing if you give only a single file",
			"Filter Files")
	{}
	static RegexMaker const singleton;
public:
	static CommandMaker const& get()
	{
		return singleton;
	}
protected:
	char const* parse_command(iter begin,size_t n,delivery& del) const override
	{
		if(del.rgxst)
		{
			return "Filter pattern already given";
		}
		try
		{
			del.rgx.assign(begin[0]);
			if(n>1)
			{
				if(begin[1][0]=='0'||begin[1][0]=='f')
				{
					del.rgxst=del.normal;
				}
				else
				{
					del.rgxst=del.inverted;
				}
			}
			else
			{
				del.rgxst=del.normal;
			}
		}
		catch(std::exception const&)
		{
			return "Invalid regex pattern";
		}
		return nullptr;
	}
};
RegexMaker const RegexMaker::singleton;

class FillRectangleGrayMaker:public SingleCommandMaker {
	FillRectangleGrayMaker():SingleCommandMaker(4,5,"Fills given rectangle with given color","Fill Rectangle Gray")
	{}
	static FillRectangleGrayMaker const singleton;
	static char const* const invalids[4];
public:
	static CommandMaker const& get()
	{
		return singleton;
	}
protected:
	char const* parse_command_h(iter begin,size_t n,delivery& del) const override
	{
		int coords[4];
		for(size_t i=0;i<4;++i)
		{
			try
			{
				coords[i]=std::stoi(begin[i]);
				if(coords[i]<0)
				{
					return "Coordinates must be non-negative";
				}
			}
			catch(std::exception const&)
			{
				return invalids[i];
			}
		}
		int color;
		if(n>4)
		{
			try
			{
				color=std::stoi(begin[4]);
				if(color<0||color>255)
				{
					return "Color must be in range [0,255]";
				}
			}
			catch(std::exception const&)
			{
				return "Invalid argument for color";
			}
		}
		else
		{
			color=255;
		}
		if(coords[0]>=coords[2])
		{
			return "Left must be less than right";
		}
		if(coords[1]>=coords[3])
		{
			return "Top must be less than bottom";
		}
		del.pl.add_process<FillSelectionAbsGray>(
			ImageUtils::Rectangle<uint>(
				{
					scast<uint>(coords[0]),
					scast<uint>(coords[2]),
					scast<uint>(coords[1]),
					scast<uint>(coords[3])
				}),
			Grayscale(color));
		return nullptr;
	}
};
FillRectangleGrayMaker const FillRectangleGrayMaker::singleton;
#define minv(name) "Invalid argument for " #name
char const* const FillRectangleGrayMaker::invalids[4]={minv(left),minv(top),minv(right),minv(bottom)};
#undef minv

class RotateMaker:public SingleCommandMaker {
	RotateMaker():SingleCommandMaker(1,1,
		"Rotates the image by the specified amount in degrees\n"
		"Counterclockwise is positive",
		"Rotate")
	{}
	static RotateMaker const singleton;
public:
	static CommandMaker const& get()
	{
		return singleton;
	}
protected:
	char const* parse_command_h(iter begin,size_t n,delivery& del) const override
	{
		float angle;
		auto res=ScoreProcessor::parse_str(angle,(*begin).c_str());
		if(res)
		{
			return "Invalid argument for angle input";
		}
		angle=fmod(angle,360);
		if(angle<0)
		{
			angle+=360;
		}
		del.pl.add_process<Rotate>(-angle);
		return nullptr;
	}
};
RotateMaker const RotateMaker::singleton;

class NumThreadMaker:public CommandMaker {
private:
	NumThreadMaker():
		CommandMaker(
			1,1,
			"Controls the number of CPU threads used when processing multiple images\n"
			"Defaults to max number supported by the CPU"
			,"Number of Threads")
	{}
	static NumThreadMaker const singleton;
public:
	static CommandMaker const& get()
	{
		return singleton;
	}
protected:
	char const* parse_command(iter begin,size_t,delivery& del) const override
	{
		if(del.num_threads)
		{
			return "Number of threads already set";
		}
		try
		{
			int n=std::stoi(*begin);
			if(n<1)
			{
				return "Number of threads must be positive";
			}
			del.num_threads=n;
			return nullptr;
		}
		catch(std::exception const&)
		{
			return "Invalid argument given for number of threads";
		}
	}
};
NumThreadMaker const NumThreadMaker::singleton;

std::unordered_map<std::string,CommandMaker const*> const init_commands()
{
	std::unordered_map<std::string,CommandMaker const*> commands;
	commands.emplace("-fg",&FilterGrayMaker::get());

	commands.emplace("-cg",&ConvertGrayMaker::get());

	commands.emplace("-ccg",&ClusterClearMaker::get());

	commands.emplace("-ccga",&ClusterClearAltMaker::get());

	commands.emplace("-hp",&HorizontalPaddingMaker::get());

	commands.emplace("-vp",&VerticalPaddingMaker::get());

	commands.emplace("-o",&OutputMaker::get());

	commands.emplace("-ap",&AutoPaddingMaker::get());

	commands.emplace("-cut",&CutMaker::get());

	commands.emplace("-spl",&SpliceMaker::get());
	commands.emplace("-splice",&SpliceMaker::get());

	commands.emplace("-bl",&BlurMaker::get());
	commands.emplace("-blur",&BlurMaker::get());

	commands.emplace("-rcg",&RescaleGrayMaker::get());

	commands.emplace("-rb",&RemoveBorderMaker::get());

	commands.emplace("-vb",&LogMaker::get());
	commands.emplace("-verbosity",&LogMaker::get());

	commands.emplace("-str",&StraightenMaker::get());
	commands.emplace("-straighten",&StraightenMaker::get());

	commands.emplace("-rs",&RescaleMaker::get());
	commands.emplace("-rescale",&RescaleMaker::get());

	commands.emplace("-flt",&RegexMaker::get());

	commands.emplace("-fr",&FillRectangleGrayMaker::get());

	commands.emplace("-nt",&NumThreadMaker::get());

	commands.emplace("-si",&StartingIndexMaker::get());

	commands.emplace("-rot",&RotateMaker::get());
	commands.emplace("-rotate",&RotateMaker::get());

	commands.emplace("-list",&ListFilesMaker::get());
	return commands;
}

auto commands=init_commands();

std::vector<std::string> conv_strings(int argc,char** argv)
{
	std::vector<std::string> ret;
	ret.reserve(argc+1);
	for(int i=0;i<argc;++i)
	{
		ret.emplace_back(argv[i]);
	}
	return ret;
}
std::vector<std::string> images_in_path(std::string const& path,std::regex const& rgx,CommandMaker::delivery::regex_state rgxst)
{
	auto ret=exlib::files_in_dir(path);
	if(rgxst)
	{
		ret.erase(std::remove_if(ret.begin(),ret.end(),
			[&rgx,keep=rgxst==CommandMaker::delivery::normal](auto const& a)
		{
			return std::regex_match(a,rgx)!=keep;
		}),ret.end());
	}
	for(auto& f:ret)
	{
		f=path+f;
	}
	return ret;
}
std::string pretty_date()
{
	std::string ret(__DATE__);
	if(ret[4]==' ')
	{
		ret[4]='0';
	}
	return ret;
}
void test()
{
	auto files=images_in_path(
		"C:\\Users\\edwar\\Documents\\Visual Studio 2017\\Projects\\ScoreProcessor\\Release\\test\\gcut\\",
		std::regex(".*"),
		CommandMaker::delivery::regex_state::normal);
	splice_pages_nongreedy(files,100,-1,300,150,
		"C:\\Users\\edwar\\Documents\\Visual Studio 2017\\Projects\\ScoreProcessor\\Release\\test\\gtogether\\p.png",
		10,1,
		1);
	stop();
}
//help screen of single input
void info_output();
//does single processes
void do_single(CommandMaker::delivery const&,std::vector<std::string> const& files,unsigned int const num_threads);
//does cut processes
void do_cut(CommandMaker::delivery const&,std::vector<std::string> const& files,unsigned int const num_threads);
//does splice processes
void do_splice(CommandMaker::delivery const&,std::vector<std::string> const& files);
//finds end of input list and gets the strings from that list
std::pair<CommandMaker::iter,std::vector<std::string>> get_files(CommandMaker::iter begin,CommandMaker::iter end);
//filters out files according to the regex pattern
void filter_out_files(std::vector<std::string>&,std::regex const& rgx,CommandMaker::delivery::regex_state);
//lists out files
void list_files(std::vector<std::string> const&);

CommandMaker::delivery parse_commands(CommandMaker::iter begin,CommandMaker::iter end);

bool could_be_command(std::string const& str)
{
	return str[0]=='-'&&str[1]>='a'&&str[1]<='z';
}
bool is_folder(std::string const& str)
{
	return str.back()=='\\'||str.back()=='/';
}

int main(int argc,char** argv)
{
#ifndef NDEBUG
	test();
#endif
	cimg::exception_mode(0);
	if(argc==1)
	{
		info_output();
		return 0;
	}
	auto args=conv_strings(argc,argv);
	auto const& arg1=args[1];
	if(could_be_command(arg1))
	{
		auto cmd=commands.find(arg1);
		if(cmd!=commands.end())
		{
			auto& m=cmd->second;
			std::cout<<m->name()<<":\n"<<m->help_message()<<'\n';
		}
		else
		{
			std::cout<<"Unknown command: "<<arg1<<'\n';
		}
		return 0;
	}
	auto arg_find=get_files(args.begin()+1,args.end());
	auto& files=arg_find.second;
	CommandMaker::delivery del;
	try
	{
		del=parse_commands(arg_find.first,args.end());
	}
	catch(std::exception const& ex)
	{
		std::cout<<ex.what()<<'\n';
		return 0;
	}
	filter_out_files(files,del.rgx,del.rgxst);
	if(del.list_files)
	{
		list_files(files);
	}
	auto num_threads=[def=del.num_threads](size_t num_files)
	{
		unsigned int cand;
		if(def)
		{
			cand=def;
		}
		else
		{
			auto nt=std::thread::hardware_concurrency();
			if(nt)
			{
				cand=nt;
			}
			else
			{
				cand=2;
			}
		}
		if(num_files>std::numeric_limits<unsigned int>::max())
		{
			num_files=std::numeric_limits<unsigned int>::max();
		}
		return std::min(cand,static_cast<unsigned int>(num_files));
	};
	switch(del.flag)
	{
		case del.do_absolutely_nothing:
			std::cout<<"No commands given"<<'\n';
			break;
		case del.do_nothing:
			[[fallthrough]];
		case del.do_single:
			do_single(del,files,num_threads(files.size()));
			break;
		case del.do_cut:
			do_cut(del,files,num_threads(files.size()));
			break;
		case del.do_splice:
			do_splice(del,files);
			break;
	}
	return 0;
}

void info_output()
{
	std::cout<<
		"Version: "<<
		pretty_date()<<
		" "
		__TIME__
		" Copyright 2017-2018 Edward Xie"
		"\n"
		"filename_or_folder... command params... ...\n"
		"place -- in front of files starting with -\n"
		"parameters that require multiple values are notated with a comma\n"
		"example: my_image.png -- -my-other-image.jpg my_folder/ -fg 180 -ccga 20,50 ,30\n"
		"Type command alone to get readme\n"
		"Available commands:\n"
		"  Single Page Operations:\n"
		"    Convert to Grayscale:     -cg\n"
		"    Filter Gray:              -fg min_value max_value=255 replacer=255\n"
		"    Cluster Clear Gray:       -ccg max_size min_size=0 background_color=255 tolerance=0.042\n"
		"    Cluster Clear Gray Alt:   -ccga required_color_range=0, bad_size_range=0, sel_range=0,200 bg_color=255\n"
		"    Horizontal Padding:       -hp left right=left\n"
		"    Vertical Padding:         -vp top bottom=top\n"
		"    Auto Padding:             -ap vert_pad min_horiz_pad max_horiz_pad horiz_offset=0 opt_ratio=1.777778\n"
		"    Rescale Colors Grayscale: -rcg min mid max\n"
		"    Blur:                     -bl radius\n"
		"    Straighten:               -str min_angle=-5 max_angle=5 angle_prec=0.1 pixel_prec=1 boundary=128\n"
		"    Remove Border (DANGER):   -rb tolerance=0.5\n"
		"    Rescale:                  -rs factor interpolation_mode=auto\n"
		"    Fill Rectangle Gray:      -fr left top right bottom color=255\n"
		"    Rotate:                   -rot degrees\n"
		"  Multi Page Operations:\n"
		"    Cut:                      -cut\n"
		"    Splice:                   -spl horiz_pad opt_pad min_vert_pad opt_height=(6/11 1st pg width) excs_weight=10 pad_weight=1\n"
		"  Options:\n"
		"    Output:                   -o format move=false\n"
		"    Verbosity:                -vb level\n"
		"    Filter Files:             -flt regex remove=false\n"
		"    Number of Threads:        -nt num\n"
		"    Starting index:           -si num\n"
		"    List Files:               -list\n"
		"Multiple Single Page Operations can be done at once. They are performed in the order they are given.\n"
		"A Multi Page Operation can not be done with other operations.\n"
		;
}

void do_single(CommandMaker::delivery const& del,std::vector<std::string> const& files,unsigned int const num_threads)
{
	del.pl.process(files,&del.sr,num_threads,del.starting_index,del.do_move);
}

void do_cut(CommandMaker::delivery const& del,std::vector<std::string> const& files,unsigned int const num_threads)
{
	class CutProcess:public exlib::ThreadTask {
	private:
		std::string const* input;
		SaveRules const* output;
		unsigned int index;
		int verbosity;
	public:
		CutProcess(std::string const* input,SaveRules const* output,unsigned int index,int verbosity):
			input(input),output(output),index(index),verbosity(verbosity)
		{}
		void execute() override
		{
			try
			{
				if(verbosity>ProcessList<>::verbosity::errors_only)
				{
					std::string coutput("Starting ");
					coutput.append(*input);
					coutput.append(1,'\n');
					std::cout<<coutput;
				}
				auto out=output->make_filename(*input,index);
				auto ext=exlib::find_extension(out.begin(),out.end());
				auto s=supported(&*ext);
				if(s==support_type::no)
				{
					if(verbosity>ProcessList<>::verbosity::silent)
					{
						std::cout<<(std::string("Unsupported file type ").append(&*ext,std::distance(ext,out.end())));
					}
					return;
				}
				CImg<unsigned char> in(input->c_str());
				ScoreProcessor::cut_page(in,out.c_str());
			}
			catch(std::exception const& ex)
			{
				if(verbosity>ProcessList<>::verbosity::silent)
				{
					std::cout<<std::string(ex.what()).append(1,'\n');
				}
			}
		}
	};
	exlib::ThreadPool tp(num_threads);
	for(size_t i=0;i<files.size();++i)
	{
		tp.add_task<CutProcess>(&files[i],&del.sr,i+del.starting_index,del.pl.get_verbosity());
	}
	tp.start();
}

void do_splice(CommandMaker::delivery const& del,std::vector<std::string> const& files)
{
	try
	{
		auto save=del.sr.make_filename(files[0],del.starting_index);
		auto ext=exlib::find_extension(save.begin(),save.end());
		if(supported(&*ext)==support_type::no)
		{
			std::cout<<std::string("Unsupported file type ")<<&*ext<<'\n';
			return;
		}
		auto num=splice_pages_nongreedy(
			files,
			del.splice_args.horiz_padding,
			del.splice_args.optimal_height,
			del.splice_args.optimal_padding,
			del.splice_args.min_padding,
			save.c_str(),
			del.splice_args.excess_weight,
			del.splice_args.padding_weight,
			del.starting_index);
		std::cout<<"Created "<<num<<" pages\n";
	}
	catch(std::exception const& ex)
	{
		std::cout<<ex.what()<<'\n';
	}
}

//finds end of input list and gets the strings from that list
std::pair<CommandMaker::iter,std::vector<std::string>> get_files(CommandMaker::iter begin,CommandMaker::iter end)
{
	bool escaped=false;
	std::pair<CommandMaker::iter,std::vector<std::string>> ret;
	auto& string=ret.first;
	auto& files=ret.second;
	for(string=begin;string!=end;++string)
	{
		if(!escaped)
		{
			if(could_be_command(*string))
			{
				return ret;
			}
			if(*string=="--")
			{
				escaped=true;
				continue;
			}
		}
		if(is_folder(*string))
		{
			auto fid=exlib::files_in_dir(*string);
			files.reserve(files.size()+fid.size());
			for(auto& str:fid)
			{
				files.emplace_back(std::move(str));
				files.back()=*string+files.back();
			}
		}
		else
		{
			files.emplace_back(std::move(*string));
		}
		escaped=false;
	}
	return ret;
}
//filters out files according to the regex pattern
void filter_out_files(std::vector<std::string>& files,std::regex const& rgx,CommandMaker::delivery::regex_state rgxst)
{
	if(rgxst)
	{
		files.erase(std::remove_if(files.begin(),files.end(),
			[&rgx,keep=rgxst==CommandMaker::delivery::normal](auto const& a)
		{
			return std::regex_match(a,rgx)!=keep;
		}),files.end());
	}
}

CommandMaker::delivery parse_commands(CommandMaker::iter arg_start,CommandMaker::iter end)
{
	CommandMaker::delivery del;
	if(arg_start!=end)
	{
		auto it=arg_start+1;
		for(;;++it)
		{
			if(it==end||could_be_command(*it))
			{
				auto cmd=commands.find(*arg_start);
				if(cmd==commands.end())
				{
					throw std::invalid_argument("Unknown command: "+*arg_start);
				}
				auto& m=cmd->second;
				if(auto res=m->make_command(arg_start+1,it,del))
				{
					throw std::invalid_argument(std::string(m->name())+" Error:\n"+res);
				}
				arg_start=it;
			}
			if(it==end)
			{
				break;
			}
		}
	}
	if(del.sr.empty())
	{
		del.sr.assign("%w");
	}
	if(!del.pl.get_log())
	{
		del.pl.set_log(&CoutLog::get());
		del.pl.set_verbosity(decltype(del.pl)::errors_only);
	}
	return del;
}

void list_files(std::vector<std::string> const& files)
{
	if(files.empty())
	{
		std::cout<<"No files found\n";
	}
	else
	{
		for(auto const& file:files)
		{
			std::cout<<file<<'\n';
		}
	}
	std::cout<<'\n';
}
